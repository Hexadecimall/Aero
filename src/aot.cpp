#define aeroRuntimeOnly
#include "main.cpp"
#include "embedded_runtime.hpp"
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#if defined(__unix__) || defined(__APPLE__)
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {
std::string number(U value) { return std::to_string(value) + "ULL"; }
std::string label(U address) { return "L" + std::to_string(address); }
std::string nativeBody(Machine& m) {
    std::set<U> pending{m.pc};
    std::map<U, std::string> blocks;
    while (!pending.empty()) {
        U here = *pending.begin(); pending.erase(pending.begin());
        if (blocks.contains(here)) continue;
        if (blocks.size() >= 1000000) fail("native translation exceeds prototype code limit");
        if (here & 3) fail("unaligned translation target");
        auto op = static_cast<std::uint32_t>(m.read(here, 4, 1));
        U next = here+4;
        unsigned rd = op & 31, rn = (op >> 5) & 31;
        bool wide = op >> 31;
        std::ostringstream s;
        auto r = [](unsigned n, bool stack = false) { return "m.reg("+std::to_string(n)+","+(stack?"true":"false")+")"; };
        auto put = [&](std::string value, bool w, bool stack = false) {
            s << "m.put(" << rd << ',' << value << ',' << (w?"true":"false") << ',' << (stack?"true":"false") << ");\n";
        };
        auto jump = [&](U target) { pending.insert(target); return "goto " + label(target) + ";\n"; };
        if (aeroInstructions::supports(op)) {
            s << "aeroInstructions::execute(m," << op << "u);\n" << jump(next);
        } else if ((op & 0xfffffc1f) == 0xd65f0000 ||
                   (op & 0xfffffc1f) == 0xd61f0000 || (op & 0xfffffc1f) == 0xd63f0000) {
            s << "m.pc=" << r(rn) << ";\n";
            if ((op & 0xfffffc1f) == 0xd63f0000) {
                s << "m.x[30]=" << number(next) << ";\n";
                pending.insert(next);
            }
            s << "goto dispatch;\n";
        } else if ((op & 0xff000010) == 0x54000000) {
            U target = here+(Machine::sext((op >> 5) & 0x7ffff,19)<<2);
            s << "if(m.condition(" << (op & 15) << ")) " << jump(target) << jump(next);
        } else if ((op & 0x7e000000) == 0x36000000) {
            unsigned bit = ((op >> 19) & 31) + ((op >> 31)*32);
            U target = here+(Machine::sext((op >> 5) & 0x3fff,14)<<2);
            s << "if(bool((" << r(rd) << ">>" << bit << ")&1)=="
              << ((op & 0x01000000)?"true":"false") << ") " << jump(target) << jump(next);
        } else if ((op & 0xffe0001f) == 0xd4000001) {
            // The system call number can be data-dependent. Only syscall dispatch remains.
            s << "if(m.x[8]==93 || m.x[8]==94) return int(m.x[0]&255);\n"
                 "if(m.x[8]!=64) fail(\"unsupported Linux syscall: \"+std::to_string(m.x[8]));\n"
                 "if(m.x[0]!=1 && m.x[0]!=2) m.x[0]=U(-9);\n"
                 "else if(!m.x[2]) m.x[0]=0;\n"
                 "else { unsigned char* data=nullptr; try { data=m.access(m.x[1],m.x[2],4); }"
                 "catch(const std::runtime_error&) {}\n"
                 "if(!data) m.x[0]=U(-14); else { auto& out=m.x[0]==1?std::cout:std::cerr;"
                 "out.write(reinterpret_cast<char*>(data),static_cast<std::streamsize>(m.x[2]));"
                 "out.flush(); m.x[0]=out?m.x[2]:U(-5); }}\n";
            // Exit may terminate a text segment. An unreachable fallthrough is emitted as a trap.
            bool executable = true;
            try { m.read(next,4,1); } catch (const std::runtime_error&) { executable=false; }
            if (executable) s << jump(next);
            else s << "fail(\"execution passed the end of guest code\");\n";
        } else if ((op & 0x7f800000) == 0x52800000) {
            unsigned shift = ((op >> 21)&3)*16;
            if (!wide && shift>=32) fail("invalid MOVZ encoding");
            put(number(U((op>>5)&65535)<<shift), wide); s << jump(next);
        } else if ((op & 0x1f000000) == 0x10000000) {
            U imm = Machine::sext(((op>>5)&0x7ffff)*4+((op>>29)&3),21);
            put(number(wide ? (here&~U(4095))+(imm<<12) : here+imm),true); s << jump(next);
        } else if ((op & 0x7c000000) == 0x14000000) {
            if(wide) { s << "m.x[30]=" << number(next) << ";\n"; pending.insert(next); }
            s << jump(here+(Machine::sext(op&0x3ffffff,26)<<2));
        } else if ((op & 0x7e000000) == 0x34000000) {
            U target=here+(Machine::sext((op>>5)&0x7ffff,19)<<2);
            s << "if ((" << r(rd) << (wide?"":" & 0xffffffffULL") << ")"
              << ((op&0x01000000)?"!=0":"==0") << ") " << jump(target) << jump(next);
        } else if ((op & 0x3f800000) == 0x11000000) {
            U imm=U((op>>10)&4095)<<((op&(1u<<22))?12:0);
            put(r(rn,true)+((op&(1u<<30))?"-":"+")+number(imm),wide,true); s << jump(next);
        } else if ((op & 0x7fe0fc00) == 0x0b000000) {
            put(r(rn)+"+"+r((op>>16)&31),wide); s << jump(next);
        } else if ((op & 0x3fc00000) == 0x39400000) {
            unsigned size=1u<<(op>>30);
            put("m.read("+r(rn,true)+"+"+number(U((op>>10)&4095)*size)+","+std::to_string(size)+")",size==8);
            s << jump(next);
        } else {
            // Preserve traps in reachable paths, including data after a non-returning exit.
            s << "fail(\"unsupported AArch64 instruction " << op << " at " << here << "\");\n";
        }
        blocks[here]=s.str();
    }
    std::string result="goto "+label(m.pc)+";\n";
    result += "dispatch: switch(m.pc) {\n";
    for(const auto& [address, code]:blocks)
        result += "case "+number(address)+": goto "+label(address)+";\n";
    result += "default: fail(\"native export reached an untranslated branch target\"); }\n";
    for(const auto& [address, code]:blocks) result+=label(address)+": {\n"+code+"}\n";
    return result;
}
}

void exportNative(const std::string& input, const std::string& output) {
#if defined(__unix__) || defined(__APPLE__)
    namespace fs = std::filesystem;
    if(fs::absolute(input).lexically_normal()==fs::absolute(output).lexically_normal() ||
       (fs::exists(output) && fs::equivalent(input,output))) fail("native output must differ from the input program");
    Machine machine; machine.load(input,{input});
    std::string body=nativeBody(machine);
    std::ifstream file(input,std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(file)),{});
    auto destination=fs::absolute(output);
    std::string pattern=(destination.parent_path()/".aero-build-XXXXXX").string();
    std::vector<char> name(pattern.begin(),pattern.end()); name.push_back(0);
    if(!mkdtemp(name.data())) fail("cannot create native build directory beside output");
    struct Cleanup { fs::path path; ~Cleanup() { std::error_code ec; fs::remove_all(path,ec); } } cleanup{fs::path(name.data())};
    auto source=cleanup.path/"program.cpp", binary=cleanup.path/"program";
    std::ofstream generated(source);
    generated << "#define aeroRuntimeOnly\n" << aeroRuntimeSource
              << "\nstatic int translated(Machine& m) {\n" << body << "}\n"
                 "int main(int argc,char** argv) { try {\n"
                 "const std::vector<unsigned char> bytes={";
    for(unsigned byte:bytes) generated << byte << ',';
    generated << "};\nMachine m; m.loadBytes(bytes,std::vector<std::string>(argv,argv+argc));"
                 "return translated(m); } catch(const std::exception& e) {"
                 "std::cerr<<\"aero: \"<<e.what()<<'\\n'; return 1; }}\n";
    generated.close(); if(!generated) fail("cannot write native translation");
    std::string sourceName=source.string(), binaryName=binary.string();
    pid_t child=fork();
    if(child<0) fail("cannot start native compiler");
    if(child==0) {
        execlp("c++","c++","-std=c++20","-O3","-x","c++",sourceName.c_str(),"-o",binaryName.c_str(),static_cast<char*>(nullptr));
        _exit(127);
    }
    int status=0;
    while(waitpid(child,&status,0)<0) if(errno!=EINTR) fail("cannot wait for native compiler");
    if(!WIFEXITED(status) || WEXITSTATUS(status)!=0) fail("native compilation failed; a working C++20 compiler named c++ is required");
    fs::rename(binary,destination);
    std::cout << "Created " << output << '\n';
#else
    (void)input; (void)output;
    fail("native export is currently supported on Unix hosts only");
#endif
}
