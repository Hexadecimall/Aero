#include "nativeCompiler.hpp"
#include <clang/CodeGen/CodeGenAction.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Tooling/Tooling.h>
#include <lld/Common/Driver.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/TargetParser/Host.h>
#include <llvm/Object/ObjectFile.h>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <vector>

#if defined(__APPLE__)
LLD_HAS_DRIVER(macho)
#elif defined(__linux__)
LLD_HAS_DRIVER(elf)
#endif

namespace {
class ObjectAction : public clang::EmitObjAction {
    std::string outputPath;
public:
    explicit ObjectAction(std::string output) : outputPath(std::move(output)) {}
    bool BeginInvocation(clang::CompilerInstance& compiler) override {
        compiler.getFrontendOpts().OutputFile = outputPath;
        return true;
    }
};
void emitObject(const std::string& source, const std::filesystem::path& output) {
    static std::once_flag initialized;
    std::call_once(initialized, [] {
        llvm::InitializeNativeTarget();
        llvm::InitializeNativeTargetAsmPrinter();
        llvm::InitializeNativeTargetAsmParser();
    });
    std::vector<std::string> arguments{"-std=c++20", "-O3", "-fPIC", "-fexceptions",
        "-fcxx-exceptions", "-nostdinc", "-nostdinc++", "-Wno-everything",
        "-target", llvm::sys::getDefaultTargetTriple(), "-o", output.string()};
#if defined(__aarch64__)
    arguments.push_back("-mno-outline-atomics");
#endif
#if defined(__APPLE__)
    arguments.push_back("-mmacosx-version-min=13.0");
#endif
    if (!clang::tooling::runToolOnCodeWithArgs(std::make_unique<ObjectAction>(output.string()),
            source, arguments, "aero-native.cpp", "aero-frontend"))
        throw std::runtime_error("in-process native compilation failed");
}
void linkNative(const std::vector<std::string>& arguments) {
    std::vector<const char*> raw;
    for (const auto& arg : arguments) raw.push_back(arg.c_str());
#if defined(__APPLE__)
    auto result = lld::lldMain(raw, llvm::outs(), llvm::errs(), {{lld::Darwin, &lld::macho::link}});
#elif defined(__linux__)
    auto result = lld::lldMain(raw, llvm::outs(), llvm::errs(), {{lld::Gnu, &lld::elf::link}});
#else
    throw std::runtime_error("native export is not supported on this host yet");
#endif
#if defined(__APPLE__) || defined(__linux__)
    if (!result.canRunAgain || result.retCode != 0)
        throw std::runtime_error("in-process native linking failed");
#endif
}
}

void compileNative(const std::string& source, const std::filesystem::path& directory,
                   const std::filesystem::path& output) {
    auto object = directory / "program.o";
#if defined(__APPLE__)
    emitObject(source, object);
#if defined(__aarch64__)
    const std::string architecture = "arm64";
#else
    const std::string architecture = "x86_64";
#endif
    // Link-only declarations name OS-provided libraries, without redistributing an SDK.
    for (const auto& library : {"libSystem.B", "libc++.1"}) {
        std::ofstream stub(directory / (std::string(library) + ".tbd"));
        stub << "--- !tapi-tbd-v3\narchs: [ " << architecture
             << " ]\nplatform: macosx\ninstall-name: '/usr/lib/" << library
             << ".dylib'\nexports:\n  - archs: [ " << architecture << " ]\n    symbols: []\n...\n";
        if (!stub) throw std::runtime_error("cannot write system-library declarations");
    }
    linkNative({"ld64.lld", "-arch", architecture, "-platform_version", "macos", "13.0", "13.0",
        "-o", output.string(), "-e", "_main", "-undefined", "dynamic_lookup",
        "-needed_library", (directory/"libSystem.B.tbd").string(),
        "-needed_library", (directory/"libc++.1.tbd").string(), object.string()});
#elif defined(__linux__)
    // A glibc entry point, compiled in-process along with the translated program.
#if defined(__x86_64__)
    const std::string startup = R"(
extern "C" __attribute__((naked,used)) void aeroStart() {
__asm__("xor %ebp,%ebp; mov %rdx,%r9; pop %rsi; mov %rsp,%rdx; and $-16,%rsp; "
        "push %rax; push %rsp; xor %r8d,%r8d; xor %ecx,%ecx; lea main(%rip),%rdi; "
        "call __libc_start_main@PLT; hlt"); }
)";
    const std::string loader = "/lib64/ld-linux-x86-64.so.2";
#elif defined(__aarch64__)
    const std::string startup = R"(
extern "C" __attribute__((naked,used)) void aeroStart() {
__asm__("mov x29,#0; mov x30,#0; mov x5,x0; ldr x1,[sp]; add x2,sp,#8; mov x6,sp; "
        "adrp x0,main; add x0,x0,:lo12:main; mov x3,#0; mov x4,#0; bl __libc_start_main; b ."); }
)";
    const std::string loader = "/lib/ld-linux-aarch64.so.1";
#else
    throw std::runtime_error("native export requires an x86-64 or AArch64 host");
#endif
#if defined(__x86_64__) || defined(__aarch64__)
    emitObject(source + "\nextern \"C\" { __attribute__((visibility(\"hidden\"))) void* __dso_handle = &__dso_handle; }\n" + startup, object);
    auto parsed = llvm::object::ObjectFile::createObjectFile(object.string());
    if (!parsed) throw std::runtime_error(llvm::toString(parsed.takeError()));
    std::string declarations;
    for (const auto& symbol : parsed->getBinary()->symbols()) {
        auto flags = symbol.getFlags();
        if (!flags) throw std::runtime_error(llvm::toString(flags.takeError()));
        if (!(*flags & llvm::object::SymbolRef::SF_Undefined)) continue;
        auto name = symbol.getName();
        if (!name) throw std::runtime_error(llvm::toString(name.takeError()));
        if (name->empty() || *name == "_GLOBAL_OFFSET_TABLE_") continue;
        for (char c : *name)
            if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                  (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '$'))
                throw std::runtime_error("unsupported native symbol name");
        auto text = name->str();
        declarations += "__asm__(\".data\\n.globl " + text + "\\n" + text + ": .byte 0\\n\");\n";
    }
    auto stubObject = directory/"imports.o";
    emitObject(declarations, stubObject);
    for (const auto& soname : {"libc.so.6", "libstdc++.so.6"})
        linkNative({"ld.lld", "-shared", "-soname", soname, stubObject.string(),
                    "-o", (directory/soname).string()});
    linkNative({"ld.lld", "-pie", "-e", "aeroStart", "--dynamic-linker", loader,
        "-o", output.string(), object.string(), "--no-as-needed",
        (directory/"libc.so.6").string(), (directory/"libstdc++.so.6").string()});
#endif
#else
    (void)source; (void)output;
    throw std::runtime_error("native export is not supported on this host yet");
#endif
}
