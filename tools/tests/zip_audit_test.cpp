// Standalone regression checks for the game's real ZIP reader and bundled inflater.
#include "unzip.h"
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

using Bytes = std::vector<unsigned char>;
static void word(Bytes& b, uint32_t v, int n) { while (n--) { b.push_back(v & 255); v >>= 8; } }
static uint32_t crc(const Bytes& data) {
    uint32_t c = 0xffffffff;
    for (auto v : data) { c ^= v; for (int i = 0; i < 8; ++i) c = (c >> 1) ^ (0xedb88320 & (0u - (c & 1))); }
    return ~c;
}
static Bytes raw_deflate(const Bytes& data) {
    // One final, uncompressed DEFLATE block; exercises real inflater framing.
    Bytes result{1}; word(result, static_cast<uint32_t>(data.size()), 2);
    word(result, ~static_cast<uint32_t>(data.size()), 2);
    result.insert(result.end(), data.begin(), data.end()); return result;
}
static Bytes archive(const Bytes& content, const Bytes& payload, unsigned declared, unsigned method) {
    Bytes b; const char* name = "test.mn3"; unsigned n = static_cast<unsigned>(strlen(name));
    word(b,0x04034b50,4); word(b,20,2); word(b,0,2); word(b,method,2); word(b,0,4);
    word(b,crc(content),4); word(b,static_cast<unsigned>(payload.size()),4); word(b,declared,4);
    word(b,n,2); word(b,0,2); b.insert(b.end(),name,name+n); b.insert(b.end(),payload.begin(),payload.end());
    unsigned offset=static_cast<unsigned>(b.size());
    word(b,0x02014b50,4); word(b,20,2); word(b,20,2); word(b,0,2); word(b,method,2); word(b,0,4);
    word(b,crc(content),4); word(b,static_cast<unsigned>(payload.size()),4); word(b,declared,4);
    word(b,n,2); word(b,0,2); word(b,0,2); word(b,0,2); word(b,0,2); word(b,0,4); word(b,0,4);
    b.insert(b.end(),name,name+n); unsigned cd_size=static_cast<unsigned>(b.size())-offset;
    word(b,0x06054b50,4); word(b,0,2); word(b,0,2); word(b,1,2); word(b,1,2);
    word(b,cd_size,4); word(b,offset,4); word(b,0,2); return b;
}
static bool check(const std::filesystem::path& root, const char* label, const Bytes& content,
    Bytes payload, unsigned declared, unsigned method, bool valid) {
    const auto path=root/(std::string(label)+".zip"); const auto output=root/(std::string(label)+".out");
    Bytes zip=archive(content,payload,declared,method);
    FILE* f=fopen(path.string().c_str(),"wb"); if (!f) return false;
    fwrite(zip.data(),1,zip.size(),f); fclose(f);
    ZIP reader; if (!reader.OpenZip(path.string().c_str())) return false;
    zipentry* e=reader.ReadNextZipEntry(); if (!e) return false;
    Bytes decoded(static_cast<size_t>(declared)+32,0xa5);
    int memory=reader.ReadFile(e,reinterpret_cast<char*>(decoded.data()+16));
    int file=reader.ExtractFile(e,output.string().c_str());
    bool ok=valid ? (memory==0 && file==0 && std::filesystem::file_size(output)==content.size() &&
        (content.empty() || !memcmp(decoded.data()+16,content.data(),content.size()))) : (memory!=0 && file!=0);
    for (int i=0;i<16;++i) ok=ok && decoded[i]==0xa5 && decoded[16+declared+i]==0xa5;
    printf("%s %s memory=%d file=%d\n",ok?"PASS":"FAIL",label,memory,file); return ok;
}
int main(int argc,char** argv) {
    if (argc==4 && !strcmp(argv[1],"unpack")) {
        ZIP reader; if (!reader.OpenZip(argv[2])) return 2;
        std::filesystem::create_directories(argv[3]); int count=0;
        while (zipentry* e=reader.ReadNextZipEntry()) {
            if (e->uncompressed_size>64*1024*1024) return 3;
            Bytes data(static_cast<size_t>(e->uncompressed_size)+32,0xa5);
            if (reader.ReadFile(e,reinterpret_cast<char*>(data.data()+16))) return 4;
            std::filesystem::path path=std::filesystem::path(argv[3])/(std::to_string(count++)+".bin");
            if (reader.ExtractFile(e,path.string().c_str())) return 5;
            FILE* f=fopen(path.string().c_str(),"rb"); if (!f) return 6;
            Bytes extracted(e->uncompressed_size);
            size_t read=fread(extracted.data(),1,extracted.size(),f); fclose(f);
            if (read!=extracted.size() || (read && memcmp(extracted.data(),data.data()+16,read))) return 7;
            for (int i=0;i<16;++i) if (data[i]!=0xa5 || data[16+e->uncompressed_size+i]!=0xa5) return 8;
        }
        printf("PASS unpacked %d entries\n",count); return count ? 0 : 9;
    }
    if (argc!=2) return 2;
    std::filesystem::path root(argv[1]); std::filesystem::create_directories(root);
    Bytes content(4096); for (size_t i=0;i<content.size();++i) content[i]=static_cast<unsigned char>(i*37);
    Bytes oneblock(content.begin(),content.begin()+1024); bool ok=true;
    ok=check(root,"stored",content,content,4096,0,true)&&ok;
    ok=check(root,"stored_empty",{},{},0,0,true)&&ok;
    ok=check(root,"deflate",content,raw_deflate(content),4096,8,true)&&ok;
    ok=check(root,"deflate_empty",{},raw_deflate({}),0,8,true)&&ok;
    ok=check(root,"short_output",oneblock,raw_deflate(oneblock),2048,8,false)&&ok;
    ok=check(root,"long_output",content,raw_deflate(content),1024,8,false)&&ok;
    Bytes truncated=raw_deflate(content); truncated.resize(truncated.size()-19);
    ok=check(root,"truncated",content,truncated,4096,8,false)&&ok;
    Bytes trailing=raw_deflate(content); trailing.insert(trailing.end(),8,0);
    ok=check(root,"trailing_compressed_data",content,trailing,4096,8,false)&&ok;
    ok=check(root,"invalid_deflate",content,{7,255,255,255},4096,8,false)&&ok;
    return ok?0:1;
}
