/*
 * Created by prabushitha on 5/19/18.
 * Copyright [2018] <ScoreLab Organization>
*/
#include "extractor.h"
#include "rlp.h"
#include "utils.h"
#include <vector>
#include <iostream>

// todo : Resize vectors (header attributes, block attributes, transaction attributes) to the required amount of memory

EtherExtractor::EtherExtractor(std::string db_path) {
    // leveldb::DB* db;
    options.create_if_missing = true;
    leveldb::Status status = leveldb::DB::Open(options, db_path, &db);
    if (!status.ok()) {
        delete db;
        throw;
    }
}
/*
*
* BLOCK RELATED PARSING
*
*/
Block EtherExtractor::getBlock(uint64_t blockNumber) {
    // We need to get the hash of the block first.
    std::string hashKey = createBlockHashKey(blockNumber);
    // create a string variable to store the hash
    std::string blockHash;

    std::string test = hexStr((unsigned char *)&hashKey[0], hashKey.size()).c_str();
    leveldb::Status shk = db->Get(leveldb::ReadOptions(), hashKey, &blockHash);

    if (shk.ok()) {
        return getBlock(blockNumber, blockHash);
    }

    throw;
}

Block EtherExtractor::getBlock(std::string blockHashHex) {
    blockHashHex = remove0xFromString(blockHashHex);

    std::vector<uint8_t > hexval = hex_to_bytes(blockHashHex);

    // block hash byte string
    std::string blockHash(hexval.begin(), hexval.end());

    // append db key prefix
    hexval.insert(hexval.begin(), blockHashPrefix[0]);
    std::string keyString(hexval.begin(), hexval.end());

    // create a string variable to store the number
    std::string blockNumber;
    // get the value from leveldb
    leveldb::Status shk = db->Get(leveldb::ReadOptions(), keyString, &blockNumber);

    if (shk.ok()) {
        uint64_t number = bytesVectorToInt(getByteVector(blockNumber));
        return getBlock(number, blockHash);
    } else {
        printf("INVALID HASH : %s\n", shk.ToString().c_str());
    }
    throw;
}

Block EtherExtractor::getBlock(uint64_t blockNumber, std::string blockHash) {
    std::string headerKey = createBlockHeaderKey(blockNumber, blockHash);

    std::string blockHeaderData;
    leveldb::Status sbhd = db->Get(leveldb::ReadOptions(), headerKey, &blockHeaderData);

    if (sbhd.ok()) {
        // Header content retrieval success
        std::vector<uint8_t> header_content;
        header_content = getByteVector(blockHeaderData);

        RLP rlp_header{header_content};
        // printf("RLP Items %d\n", rlp_header.numItems());

        Header h;
        updateHeader(&h, header_content, rlp_header);

        // create a new block and add the header
        Block block(h);
        block.hash_bytes = getByteVector(blockHash);

        // Get block body content
        std::string bodyKey = createBlockBodyKey(blockNumber, blockHash);

        std::string blockBodyData;
        leveldb::Status sbbd = db->Get(leveldb::ReadOptions(), bodyKey, &blockBodyData);

        if (sbbd.ok()) {
            // print_bytes(blockBodyData);
            // Header content retrieval success
            std::vector<uint8_t> body_content;
            body_content = getByteVector(blockBodyData);

            RLP rlp_body{body_content};
            updateBody(&block, body_content, rlp_body);
        } else {
            printf("\n Body Not Found \n");
        }

        // get block tx receipt data

        return block;
    }

    throw;
}
// get transaction receipt
TransactionReceipt EtherExtractor::getTransactionReceipt(std::string transactionHashHex) {
    transactionHashHex = remove0xFromString(transactionHashHex);

    // creating block hash byte string
    std::vector<uint8_t > hexval = hex_to_bytes(transactionHashHex);
    std::string txHash(hexval.begin(), hexval.end());

    // append db key prefix
    std::string lookupKey = createLookupKey(txHash);

    std::string txReceiptMetaData;
    leveldb::Status srmd = db->Get(leveldb::ReadOptions(), lookupKey, &txReceiptMetaData);

    if (srmd.ok()) {
        std::vector<uint8_t> receipt_contents;
        receipt_contents = getByteVector(txReceiptMetaData);

        RLP rlp_receipts{receipt_contents};

        TransactionReceiptMeta meta;
        updateTxReceiptMeta(&meta, receipt_contents, rlp_receipts);
        if (meta.status == 1) {
            TransactionReceipt txReceipt = getBlockTxReceipts(meta.blockNumber, meta.blockHash)[meta.index];
            return txReceipt;
        }
    }
    throw;
}
// get tx receipts of the block
std::vector<TransactionReceipt> EtherExtractor::getBlockTxReceipts(uint64_t blockNumber, std::string blockHash) {
    std::string receiptsKey = createBlockReceiptKey(blockNumber, blockHash);

    std::string blockReceiptData;
    leveldb::Status sbrd = db->Get(leveldb::ReadOptions(), receiptsKey, &blockReceiptData);

    if (sbrd.ok()) {
        std::vector<uint8_t> receipt_contents;
        receipt_contents = getByteVector(blockReceiptData);

        RLP rlp_receipts{receipt_contents};

        std::vector<TransactionReceipt> receipts;

        updateTxReceipts(&receipts, blockNumber, getByteVector(blockHash), receipt_contents, rlp_receipts);

        return receipts;
    }
    return {};
}

// create block hash key
std::string EtherExtractor::createBlockHashKey(uint64_t blockNumber) {
    return getKeyString(headerPrefix, &toBigEndianEightBytes(blockNumber)[0], numSuffix, 1, 8, 1);
}

// get block header key
std::string EtherExtractor::createBlockHeaderKey(uint64_t blockNumber, std::string blockHash) {
    return getKeyString(headerPrefix, &toBigEndianEightBytes(blockNumber)[0], reinterpret_cast<uint8_t *>(&blockHash[0]), 1, 8, 32);
}

// get block body key
std::string EtherExtractor::createBlockBodyKey(uint64_t blockNumber, std::string blockHash) {
    return getKeyString(bodyPrefix, &toBigEndianEightBytes(blockNumber)[0], reinterpret_cast<uint8_t *>(&blockHash[0]), 1, 8, 32);
}

// get block receipts key
std::string EtherExtractor::createBlockReceiptKey(uint64_t blockNumber, std::string blockHash) {
    return getKeyString(blockReceiptsPrefix, &toBigEndianEightBytes(blockNumber)[0], reinterpret_cast<uint8_t *>(&blockHash[0]), 1, 8, 32);
}

// get tx lookup key
std::string EtherExtractor::createLookupKey(std::string transactionHash) {
    return getKeyString(lookupPrefix, reinterpret_cast<uint8_t *>(&transactionHash[0]), {}, 1, 32, 0);
}

void EtherExtractor::updateHeader(Header *header, std::vector<uint8_t> contents, RLP & rlp) {
    header->parentHash_bytes = createByteVector(contents, rlp[0]);
    header->sha3Uncles_bytes = createByteVector(contents, rlp[1]);
    header->beneficiary_bytes = createByteVector(contents, rlp[2]);
    header->stateRoot_bytes = createByteVector(contents, rlp[3]);
    header->transactionsRoot_bytes = createByteVector(contents, rlp[4]);
    header->receiptsRoot_bytes = createByteVector(contents, rlp[5]);
    header->logsBloom_bytes = createByteVector(contents, rlp[6]);
    header->difficulty_bytes = createByteVector(contents, rlp[7]);
    header->number_bytes = createByteVector(contents, rlp[8]);
    header->gasLimit_bytes = createByteVector(contents, rlp[9]);
    header->gasUsed_bytes = createByteVector(contents, rlp[10]);
    header->timestamp_bytes = createByteVector(contents, rlp[11]);
    header->extraData_bytes = createByteVector(contents, rlp[12]);
    header->mixHash_bytes = createByteVector(contents, rlp[13]);
    header->nonce_bytes = createByteVector(contents, rlp[14]);
}

void EtherExtractor::updateBody(Block *block, std::vector<uint8_t> contents, RLP & rlp) {
    if (rlp.numItems() > 1) {
        int i;
        // get transactions
        for (i = 0; i < rlp[0].numItems(); i++) {
            // todo : check if transaction contains valid number of fields
            Transaction transaction;
            transaction.nonce_bytes = createByteVector(contents, rlp[0][i][0]);
            transaction.gasPrice_bytes = createByteVector(contents, rlp[0][i][1]);
            transaction.gasLimit_bytes = createByteVector(contents, rlp[0][i][2]);
            transaction.to_bytes = createByteVector(contents, rlp[0][i][3]);  // empty for contract creation
            transaction.value_bytes = createByteVector(contents, rlp[0][i][4]);
            transaction.init_bytes = createByteVector(contents, rlp[0][i][5]);

            transaction.v_bytes = createByteVector(contents, rlp[0][i][6]);
            transaction.r_bytes = createByteVector(contents, rlp[0][i][7]);
            transaction.s_bytes = createByteVector(contents, rlp[0][i][8]);

            // calculate the hash of the transaction
            std::vector<uint8_t> rlp_encoded_tx = rlp[0][i].serializedData();
            transaction.hash_bytes = keccak_256(rlp_encoded_tx);

            transaction.from_bytes = transaction.recoverTxSender();

            if (transaction.from_bytes.size() == 0) {
                printf("Block %d Transaction %d Address Not Found\n", bytesVectorToInt(block->header.number_bytes), i+1);
            }

            block->transactions.insert(block->transactions.end(), transaction);
        }

        // todo : check below code after a full node.
        // get ommers
        std::vector<std::vector<uint8_t>> ommerHashes;

        for (i = 0; i < rlp[1].numItems(); i++) {
            std::vector<uint8_t> ommerHash = createByteVector(contents, rlp[1][i]);
            ommerHashes.insert(ommerHashes.end(), ommerHash);
        }
        block->ommerHashes_bytes = ommerHashes;
    }
}

void EtherExtractor::updateTxReceiptMeta(TransactionReceiptMeta *meta, std::vector<uint8_t> contents, RLP & rlp) {
    if (rlp.numItems() == 3) {
        std::vector<uint8_t> tempVec =  createByteVector(contents, rlp[0]);
        meta->blockHash = std::string(tempVec.begin(), tempVec.end());

        meta->blockNumber = (uint64_t)bytesVectorToInt(createByteVector(contents, rlp[1]));

        meta->index = (uint64_t)bytesVectorToInt(createByteVector(contents, rlp[2]));

        meta->status = 1;
    }
}

void EtherExtractor::updateTxReceipts(std::vector<TransactionReceipt> *receipts, uint64_t blockNumber, std::vector<uint8_t> blockHash, std::vector<uint8_t> contents, RLP & rlp) {
    int i;
    for (i = 0; i < rlp.numItems(); i++) {
        TransactionReceipt receipt;

        receipt.blockNumber = blockNumber;
        receipt.transactionIndex = i;
        receipt.blockHash_bytes = blockHash;

        // get transactions
        receipt.status_bytes = createByteVector(contents, rlp[i][0]);
        receipt.cumulativeGasUsed_bytes = createByteVector(contents, rlp[i][1]);
        receipt.logsBloom_bytes = createByteVector(contents, rlp[i][2]);
        receipt.transactionHash_bytes = createByteVector(contents, rlp[i][3]);
        receipt.contractAddress_bytes = createByteVector(contents, rlp[i][4]);
        // receipt.logs; // array, won't consider decoding in this phase
        receipt.gasUsed_bytes = createByteVector(contents, rlp[i][6]);

        receipts->insert(receipts->end(), receipt);
    }
}

std::vector<uint8_t> EtherExtractor::createByteVector(std::vector<uint8_t> contents, const RLP & rlp) {
    std::vector<uint8_t>::const_iterator first = contents.begin() + rlp.dataOffset();
    std::vector<uint8_t>::const_iterator last = contents.begin() + rlp.dataOffset() + rlp.dataLength();
    std::vector<uint8_t> newVec(first, last);
    return newVec;
}

/*
*
*   ACCOUNT RELATED PARSING
*
*/
Account EtherExtractor::getAccount(std::string address) {
    // todo : get the latest block number
    uint64_t latestBlock = 100;
    return getAccount(address, latestBlock);
}

/*
 * Function to get account from state given a block height
 * todo : Not implemented. has to implement using merkel patricia tree
 */
Account EtherExtractor::getAccount(std::string address, uint64_t blockHeight) {
    Block b = getBlock(blockHeight);

    Account account;

    return account;
}
