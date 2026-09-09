#include "formats/iam.h"
#include "platform/datafs.h"
#include <cstdio>
#include <cstring>
#include <string>
int main(int argc,char**argv){
    omk::DataFs fs(argv[1]);
    for(const char* a:{"AREA","SCENE"}){
        auto raw=fs.read(std::string("IAM/")+a);
        auto ar=omk::IamArchive::open(raw);
        for(std::size_t i=0;i<ar.size();++i){
            auto sp=ar.chunk(i); if(sp.empty())continue;
            std::string b(reinterpret_cast<const char*>(sp.data()),sp.size());
            if(b.find("SMARKET1")!=std::string::npos||b.find("ASMARKT1")!=std::string::npos)
                std::printf("%s chunk %zu size %zu\n",a,i,sp.size());
        }
    }
}
