// The stand-in game's entry point as the real game client has it, for the content loader's tests (never shipped): the
// game's entry is Steam's DRM stub (SteamStub) in a section named ".bind", starting "call $+5; push rax; push rbx;
// push rcx; push rdx". Linked with /ENTRY:standin_entry, this stub has that shape and then goes on to the C runtime's
// start-up. With STANDIN_ENTRY_JUMP it is instead another tool's patch, "jmp [rip+0]" (FF 25), which the loader must
// refuse to hold.
#include <windows.h>

extern "C" DWORD WINAPI wmainCRTStartup(LPVOID);

#pragma section(".bind", read, execute)
#pragma pack(push, 1)
#ifndef STANDIN_ENTRY_JUMP
struct EntryStub {
    unsigned char head[17];   // call $+5; push rax/rbx/rcx/rdx; pop them; add rsp, 8 (drops call's return address)
    unsigned char movabs[2];  // mov rax, imm64
    DWORD(WINAPI* target)(LPVOID);  // a function pointer: a constant, so it is in place before any code runs
    unsigned char jmp[2];         // jmp rax
};
extern "C" __declspec(allocate(".bind")) const EntryStub standin_entry = {
    {0xE8, 0x00, 0x00, 0x00, 0x00, 0x50, 0x53, 0x51, 0x52, 0x5A, 0x59, 0x5B, 0x58, 0x48, 0x83, 0xC4, 0x08},
    {0x48, 0xB8},
    &wmainCRTStartup,
    {0xFF, 0xE0}};
#else
struct EntryStub {
    unsigned char jmp[6];  // jmp qword ptr [rip+0]
    DWORD(WINAPI* target)(LPVOID);
};
extern "C" __declspec(allocate(".bind")) const EntryStub standin_entry = {{0xFF, 0x25, 0x00, 0x00, 0x00, 0x00},
                                                                         &wmainCRTStartup};
#endif
#pragma pack(pop)
