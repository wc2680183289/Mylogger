#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <streambuf>
#include <string>
#include <vector>

#include "decode_formatter.h"
#include "zstd_compress.h"
#include "aes_crypt.h"
#include "effective_sink.h"

using namespace logger;
using namespace logger::detail;

std::unique_ptr<DecodeFormatter> decode_formatter;

std::vector<char> ReadFile(const std::string& file_path) {
    std::ifstream ifs(file_path, std::ios::binary);
    if(!ifs) {
        throw std::runtime_error("ReadFile: open file failed");
    }
    ifs.seekg(0, std::ios::end);
    auto size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);
    std::vector<char> buf(size);
    ifs.read(buf.data(), size);
    return buf;
}

void AppendDataToFile(const std::string& file_path, const std::string& data) {
    std::ofstream ofs(file_path, std::ios::binary | std::ios::app);
    ofs.write(data.data(), data.size());
}

std::unique_ptr<logger::compress::Compression> decompress;

void DecodeItemData(char* data, size_t size, crypt::Crypt* crypt, std::string& output) {
    //由于写日志时是先压缩后加密，所以读取日志的时候要先解密后解压，最后反序列化
    std::string decryped = crypt->Decrypt(data, size);
    std::string decompressed = decompress->Decompress(decryped.data(), decryped.size());
    EffectiveMsg msg;
    msg.ParseFromString(decompressed);
    std::string dest;
    decode_formatter->Format(msg, dest);
    output.append(dest);
}

void DecodeChunkData(char* data, size_t size, const std::string& cli_pub_key,
                     const std::string& ser_pri_key,
                     std::string& output) {
    std::cout << "decode chunk " << size << std::endl;
    std::string ser_pri_key_bin = crypt::HexKeyToBinary(ser_pri_key);
    std::string shared_secret = crypt::GenECDHSharedSecret(ser_pri_key_bin, cli_pub_key);
    //std::cout << "DecodeChunkData: shared_secret = " << logger::crypt::BinaryKeyToHex(shared_secret) <<"\n";
    std::unique_ptr<crypt::Crypt> crypt = std::make_unique<crypt::AESCrypt>(shared_secret);
    size_t offset = 0;
    size_t count = 0;
    //一个chunk由多个item构成
    while(offset < size) {
        ++count;
        if(count % 100 == 0) {
            std::cout << "decode item " << count << "\n"; 
        }
        ItemHeader* item_header = reinterpret_cast<ItemHeader*>(data + offset);
        //根据item_header携带的size信息切割每个item，通过魔数判断当前item是否正常
        if(item_header->magic != ItemHeader::kMagic) {
            throw std::runtime_error("DecodeChunkData: invalid item magic");
            return;
        }
        offset += sizeof(ItemHeader);
        // std::cout << "item_header->magic = " << item_header->magic << "\n";
        // std::cout << "item_header->size = " << item_header->size << "\n";
        // std::cout << "item_header->size % 16 = " << item_header->size % 16 << "\n";
        DecodeItemData(data + offset, item_header->size, crypt.get(), output);
        offset += item_header->size;
        output.push_back('\n');
    }
}

void DecodeFile(const std::string& input_file_path, const std::string& pri_key, const std::string& output_file_path) {
    auto input = ReadFile(input_file_path);
    if (input.size() < sizeof(ChunkHeader)) {
        throw std::runtime_error("DecodeFile: input file is too small");
        return;
    }
    auto chunk_header = reinterpret_cast<ChunkHeader*>(input.data());
    if(chunk_header->magic != ChunkHeader::kMagic) {
        throw std::runtime_error("DecodeFile: invalid chunk magic");
        return;
    }
    size_t offset = 0;
    size_t file_size = input.size();
    std::string output;
    output.resize(1024 * 1024);
    while(offset < file_size) {
        ChunkHeader* chunk_header = reinterpret_cast<ChunkHeader*>(input.data() + offset);
        if(chunk_header->magic != ChunkHeader::kMagic) {
            throw std::runtime_error("DecodeFile: invalid chunk magic");
            return;
        }
        output.clear();

        offset += sizeof(ChunkHeader);
        // std::cout << "offset after skip header = " << offset <<"\n";
        // std::cout << "first 8 bytes at data: ";
        // for(int i = 0; i < 8; i++) {
        //     printf("%02x", (unsigned char)input[offset + i]);
        // }
        // std::cout << "\n";
        //这里的std::string(chunk_header->pub_key, 65)是因为secp256r1未压缩公钥正好是65字节
        DecodeChunkData(input.data() + offset, chunk_header->size, std::string(chunk_header->pub_key, 65), pri_key, output);
        offset += chunk_header->size;
        AppendDataToFile(output_file_path, output);
    }
}

int main(int argc, char* argv[]){
    // ./decode <file_path> <pri_key> <output_file>
#if 1
    if(argc != 4) {
        std::cerr << "usege: ./decode <file_path> <pri_key> <output_file>\n";
        return 1;
    }
    std::string input_file_path = argv[1];
    std::string pri_key = argv[2];
    std::string output_file_path = argv[3];
#else
    std::string input_file_path = "/tmp/mylogger_test/demo_20260801091043.log";
    std::string pri_key = "FAA5BBE9017C96BF641D19D0144661885E831B5DDF52539EF1AB4790C05E665E";
    std::string output_file_path = "/tmp/mylogger_test/d.txt";
#endif
    try {
        decode_formatter = std::make_unique<DecodeFormatter>();
        decode_formatter->SetPattern("[%l][%D:%S][%p:%t][%F:%f:%#]%v");
        decompress = std::make_unique<logger::compress::ZstdCompress>();
        DecodeFile(input_file_path, pri_key, output_file_path);
    } catch (const std::exception& e) {
        std::cerr << "Decode failed: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}