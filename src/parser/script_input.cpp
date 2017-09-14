//
//  script_input.cpp
//  blocksci
//
//  Created by Harry Kalodner on 3/18/17.
//
//

#define BLOCKSCI_WITHOUT_SINGLETON

#include "script_input.hpp"
#include "address_writer.hpp"
#include "preproccessed_block.hpp"
#include <blocksci/scripts/bitcoin_pubkey.hpp>

#include <secp256k1.h>

#include <iostream>

ScriptInput<blocksci::AddressType::Enum::SCRIPTHASH>::ScriptInput(const InputInfo &inputInfo, const RawTransaction &, const AddressWriter &) {
    CScript script(inputInfo.getScript());
    
    CScript::const_iterator pc1 = script.begin();
    CScript::const_iterator prevpc = script.begin();
    CScript::const_iterator prevprevpc = script.begin();
    opcodetype opcode1;
    std::vector<unsigned char> vch1;
    
    std::vector<unsigned char> lastScript;
    while(true) {
        prevprevpc = prevpc;
        prevpc = pc1;
        lastScript = vch1;
        if(!script.GetOp(pc1, opcode1, vch1)) {
            break;
        }
    }
    
    CScript::const_iterator begin = script.begin();
    wrappedInputBegin = &*begin;
    wrappedInputLength = std::distance(begin, prevprevpc);
    
    CScript wrappedOutputScript(lastScript.begin(), lastScript.end());
    auto outputScriptBegin = reinterpret_cast<const unsigned char *>(lastScript.data());
    wrappedScriptOutput = extractScriptData(outputScriptBegin, outputScriptBegin + lastScript.size(), inputInfo.witnessActivated);
}

template<blocksci::AddressType::Enum type>
struct P2SHInputFunctor {
    static ScriptInputType f(uint32_t addressNum, const InputInfo &info, const RawTransaction &tx, AddressState &state, AddressWriter &addressWriter) {
        auto input = ScriptInput<type>(info, tx, addressWriter);
        input.processInput(addressNum, info, tx, state, addressWriter);
        return std::move(input);
    }
};

ScriptInputType p2shInputVisitor(const blocksci::Address &address, const InputInfo &info, const RawTransaction &tx, AddressState &state, AddressWriter &addressWriter) {
    
    static constexpr auto table = blocksci::make_dynamic_table<blocksci::AddressType, P2SHInputFunctor>();
    static constexpr std::size_t size = blocksci::AddressType::all.size();
    
    auto index = static_cast<size_t>(address.type);
    if (index >= size)
    {
        throw std::invalid_argument("combination of enum values is not valid");
    }
    return table[index](address.addressNum, info, tx, state, addressWriter);
}

struct ProcessP2SHVisitor : public boost::static_visitor<std::pair<blocksci::Address, bool>> {
    AddressState &state;
    ProcessP2SHVisitor(AddressState &state_) : state(state_) {}
    template <blocksci::AddressType::Enum type>
    std::pair<blocksci::Address, bool> operator()(ScriptOutput<type> &scriptOutput) const {
        std::pair<blocksci::Address, bool> processed = getAddressNum(scriptOutput, state);
        if (processed.second) {
            scriptOutput.processOutput(state);
        }
        return processed;
    }
};

void ScriptInput<blocksci::AddressType::Enum::SCRIPTHASH>::processInput(uint32_t addressNum, const InputInfo &inputInfo, const RawTransaction &tx, AddressState &state, AddressWriter &writer) {
    ProcessP2SHVisitor visitor(state);
    std::pair<blocksci::Address, bool> processed = boost::apply_visitor(visitor, wrappedScriptOutput);
    wrappedAddress = processed.first;
    writer.serialize(*this, addressNum);
    if (processed.second) {
        writer.serialize(wrappedScriptOutput);
    }
    
    InputInfo p2shInputInfo{inputInfo.inputNum, wrappedInputBegin, wrappedInputLength, inputInfo.witnessStack, inputInfo.witnessActivated};
    p2shInputVisitor(wrappedAddress, p2shInputInfo, tx, state, writer);
}

template<blocksci::AddressType::Enum type>
struct CheckP2SHInputFunctor {
    static ScriptInputType f(const InputInfo &info, const RawTransaction &tx, const AddressState &state, const AddressWriter &addressWriter) {
        auto input = ScriptInput<type>(info, tx, addressWriter);
        input.checkInput(info, tx, state, addressWriter);
        return std::move(input);
    }
};

ScriptInputType checkP2SHInputVisitor(blocksci::AddressType::Enum type, const InputInfo &info, const RawTransaction &tx, const AddressState &state, const AddressWriter &addressWriter) {
    
    static constexpr auto table = blocksci::make_dynamic_table<blocksci::AddressType, CheckP2SHInputFunctor>();
    static constexpr std::size_t size = blocksci::AddressType::all.size();
    
    auto index = static_cast<size_t>(type);
    if (index >= size)
    {
        throw std::invalid_argument("combination of enum values is not valid");
    }
    return table[index](info, tx, state, addressWriter);
}


struct CheckP2SHVisitor : public boost::static_visitor<std::pair<blocksci::Address, bool>> {
    const AddressState &state;
    CheckP2SHVisitor(const AddressState &state_) : state(state_) {}
    template <blocksci::AddressType::Enum type>
    std::pair<blocksci::Address, bool> operator()(ScriptOutput<type> &scriptOutput) const {
        std::pair<blocksci::Address, bool> processed = checkAddressNum(scriptOutput, state);
        if (processed.second) {
            scriptOutput.checkOutput(state);
        }
        return processed;
    }
};

void ScriptInput<blocksci::AddressType::Enum::SCRIPTHASH>::checkInput(const InputInfo &inputInfo, const RawTransaction &tx, const AddressState &state, const AddressWriter &writer) {
    CheckP2SHVisitor visitor(state);
    std::pair<blocksci::Address, bool> processed = boost::apply_visitor(visitor, wrappedScriptOutput);
    wrappedAddress = processed.first;
    InputInfo p2shInputInfo{inputInfo.inputNum, wrappedInputBegin, wrappedInputLength, inputInfo.witnessStack, inputInfo.witnessActivated};
    checkP2SHInputVisitor(wrappedAddress.type, p2shInputInfo, tx, state, writer);
}

ScriptInput<blocksci::AddressType::Enum::PUBKEYHASH>::ScriptInput(const InputInfo &inputInfo, const RawTransaction &, const AddressWriter &) {
    if (inputInfo.scriptLength > 0) {
        auto script = inputInfo.getScript();
        auto pc = script.begin();
        opcodetype opcode;
        std::vector<unsigned char> vchSig;
        script.GetOp(pc, opcode, vchSig);
        script.GetOp(pc, opcode, vchSig);
        pubkey.Set(vchSig.begin(), vchSig.end());
    } else {
        auto &pubkeyWitness = inputInfo.witnessStack[1];
        pubkey.Set(pubkeyWitness.itemBegin, pubkeyWitness.itemBegin + pubkeyWitness.length);
    }
}

void ScriptInput<blocksci::AddressType::Enum::PUBKEYHASH>::processInput(uint32_t addressNum, const InputInfo &, const RawTransaction &, AddressState &, AddressWriter &writer) {
    writer.serialize(*this, addressNum);
}

ScriptInput<blocksci::AddressType::Enum::MULTISIG>::ScriptInput(const InputInfo &, const RawTransaction &, const AddressWriter &) {
    // Prelimary work on code to track multisig spend sets
    /*
    auto &multisigFile = writer.getFile<blocksci::AddressType::Enum::MULTISIG>();
    auto &pubkeyFile = writer.getFile<blocksci::AddressType::Enum::PUBKEY>();
    auto multisigData = multisigFile.getData(inputInfo.address.addressNum - 1);
    auto addresses = multisigData->getAddresses();
    
    auto script = inputInfo.getScript();
    
    CScript::const_iterator pc1 = script.begin();
    opcodetype opcode1;
    std::vector<unsigned char> vchSig;
    script.GetOp(pc1, opcode1, vchSig);
    
    size_t pubkeyNum = 0;
    
    while(true) {
        if(!script.GetOp(pc1, opcode1, vchSig)) {
            break;
        }
        if (vchSig.size() > 0) {
            int hashType = vchSig.back();
            vchSig.pop_back();
            auto txHash = tx.getHash(inputInfo, hashType);
            
            while (pubkeyNum < addresses.size()) {
                auto pubkeyAddress = pubkeyFile.getData(addresses[pubkeyNum].addressNum - 1);
                if (pubkeyAddress->pubkey.Verify(txHash, vchSig)) {
                    spendSet[pubkeyNum] = 1;
                    pubkeyNum++;
                    break;
                } else {
                    spendSet[pubkeyNum] = 0;
                    pubkeyNum++;
                }
            }
        }
    }
    
    while (pubkeyNum < addresses.size()) {
        spendSet[pubkeyNum] = 0;
        pubkeyNum++;
    }
    */
}

void ScriptInput<blocksci::AddressType::Enum::MULTISIG>::processInput(uint32_t addressNum, const InputInfo &, const RawTransaction &, AddressState &, AddressWriter &writer) {
    writer.serialize(*this, addressNum);
}

ScriptInput<blocksci::AddressType::Enum::NONSTANDARD>::ScriptInput(const InputInfo &inputInfo, const RawTransaction &, const AddressWriter &) {
    if (inputInfo.scriptLength > 0) {
        script = inputInfo.getScript();
    } else if (inputInfo.witnessStack.size() > 0) {
        for (size_t i = 0; i < inputInfo.witnessStack.size() - 1; i++) {
            auto &stackItem = inputInfo.witnessStack[i];
            auto itemBegin = reinterpret_cast<const unsigned char *>(stackItem.itemBegin);
            script << std::vector<unsigned char>{itemBegin, itemBegin + stackItem.length};
        }
    }
}

void ScriptInput<blocksci::AddressType::Enum::NONSTANDARD>::processInput(uint32_t addressNum, const InputInfo &, const RawTransaction &, AddressState &, AddressWriter &writer) {
    writer.serialize(*this, addressNum);
}

ScriptInput<blocksci::AddressType::Enum::NULL_DATA>::ScriptInput(const InputInfo &, const RawTransaction &, const AddressWriter &) {
}

void ScriptInput<blocksci::AddressType::Enum::NULL_DATA>::processInput(uint32_t addressNum, const InputInfo &, const RawTransaction &, AddressState &, AddressWriter &writer) {
    writer.serialize(*this, addressNum);
}

ScriptInput<blocksci::AddressType::Enum::WITNESS_PUBKEYHASH>::ScriptInput(const InputInfo &inputInfo, const RawTransaction &, const AddressWriter &) {
    auto &pubkeyWitness = inputInfo.witnessStack[1];
    pubkey.Set(pubkeyWitness.itemBegin, pubkeyWitness.itemBegin + pubkeyWitness.length);
}

void ScriptInput<blocksci::AddressType::Enum::WITNESS_PUBKEYHASH>::processInput(uint32_t addressNum, const InputInfo &, const RawTransaction &, AddressState &, AddressWriter &writer) {
    writer.serialize(*this, addressNum);
}


ScriptInput<blocksci::AddressType::Enum::WITNESS_SCRIPTHASH>::ScriptInput(const InputInfo &inputInfo, const RawTransaction &, const AddressWriter &) {
    auto &witnessScriptItem = inputInfo.witnessStack.back();
    auto outputBegin = reinterpret_cast<const unsigned char *>(witnessScriptItem.itemBegin);
    wrappedScriptOutput = extractScriptData(outputBegin, outputBegin + witnessScriptItem.length, inputInfo.witnessActivated);
}

void ScriptInput<blocksci::AddressType::Enum::WITNESS_SCRIPTHASH>::processInput(uint32_t addressNum, const InputInfo &inputInfo, const RawTransaction &tx, AddressState &state, AddressWriter &writer) {
    ProcessP2SHVisitor visitor(state);
    std::pair<blocksci::Address, bool> processed = boost::apply_visitor(visitor, wrappedScriptOutput);
    wrappedAddress = processed.first;
    writer.serialize(*this, addressNum);
    if (processed.second) {
        writer.serialize(wrappedScriptOutput);
    }
    
    InputInfo p2shInputInfo{inputInfo.inputNum, inputInfo.scriptBegin, 0, inputInfo.witnessStack, inputInfo.witnessActivated};
    p2shInputVisitor(wrappedAddress, p2shInputInfo, tx, state, writer);
}

void ScriptInput<blocksci::AddressType::Enum::WITNESS_SCRIPTHASH>::checkInput(const InputInfo &inputInfo, const RawTransaction &tx, const AddressState &state, const AddressWriter &writer) {
    CheckP2SHVisitor visitor(state);
    std::pair<blocksci::Address, bool> processed = boost::apply_visitor(visitor, wrappedScriptOutput);
    wrappedAddress = processed.first;
    InputInfo p2shInputInfo{inputInfo.inputNum, inputInfo.scriptBegin, 0, inputInfo.witnessStack, inputInfo.witnessActivated};
    checkP2SHInputVisitor(wrappedAddress.type, p2shInputInfo, tx, state, writer);
}
