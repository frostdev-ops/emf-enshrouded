#pragma once
// Which loaders and DLL mods sit next to the game. emf\known_tools.txt (shipped with EMF, read by the installer too)
// has one line per tool: "word | name | text:<in its DLL> | beside:<file or folder next to the exe> |
// same-loader:<another proxy name the same loader also uses> | helper:<a proxy name of its own that does not start it
// alone>". The core reports at start-up which tool owns each proxy DLL, what EMF chain-loaded, what ModLoader Proxy
// starts, and a loader whose files another tool replaced (and which tool holds the name it wants).
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace emf::core {

struct KnownTool {
    std::string word;                      // short file-name word (the installer names a renamed DLL <word>.dll)
    std::string name;                      // shown to players
    std::vector<std::string> texts;        // text found inside its DLLs (case ignored; stored in lower case)
    std::vector<std::string> beside;       // a file or folder of its own next to the exe
    std::vector<std::string> same_loader;  // proxy names through which this one loader also runs
    std::vector<std::string> helpers;      // its proxy names that do not start it alone (helpers of its loader)
};

std::vector<KnownTool> parse_known_tools(const std::string& text);

// The word of the first tool whose text the DLL contains, as plain text or as UTF-16 (a DLL built with wide strings);
// "emf" for EMF's own loader (EMF_LOADER_MARKER); "" when unknown.
std::string tool_for_dll_text(const std::vector<KnownTool>& tools, const std::string& dll_text);

// The DLLs chain_status.txt reports as started.
std::vector<std::string> started_chain_names(const std::string& chain_status);

// ---- ModLoader Proxy (Nexus mod 162, github.com/Entrodor/modloader-proxy) ----
// A chain loader that players name dinput8.dll (or powrprof.dll on servers). It starts the DLLs its chainloader.ini
// lists, next to it: [Settings] ForwardTo = <dll> first, then [Load] (one DLL per line, ';' or '#' starts a comment
// line, " ;" a comment after the name), then with LoadFromFolder = true every *.dll in ModsFolder (default mods, not
// recursive, sorted by name). EMF reads the file only to tell which DLLs run; ModLoader Proxy reads it itself.
constexpr const char* kModLoaderProxy = "modloader-proxy";  // its word in known_tools.txt

struct ChainloaderIni {
    std::vector<std::string> load;  // [Load] entries in order, '/' written as '\'
    std::string forward_to;         // [Settings] ForwardTo
    bool load_from_folder = false;  // [Settings] LoadFromFolder
    std::string mods_folder = "mods";
};
ChainloaderIni parse_chainloader_ini(const std::string& text);

// The DLLs ModLoader Proxy starts, as paths relative to its folder, in its order and each once; never a DLL with its
// own file name (self, "" for none). dlls_in(folder) gives the *.dll file names in the mods folder.
std::vector<std::string> chainloader_dlls(const std::string& ini_text, const std::string& self,
                                          const std::function<std::vector<std::string>(const std::string&)>& dlls_in);

struct StartedDll {
    std::string name;    // as listed
    std::string word;    // its tool ("" unknown)
    bool found = false;  // the file is there
};

struct GameFolderView {
    std::vector<std::pair<std::string, std::string>> proxies;  // proxy-named DLLs next to the exe: name, tool word
    std::vector<std::string> chained_tools;                    // tool words of the DLLs EMF chain-loaded
    std::vector<std::string> beside;                           // tools' own files/folders that exist
    std::string chain_status;                                  // emf\chain_status.txt
    std::vector<StartedDll> loader_started;                    // the DLLs ModLoader Proxy starts, when it runs
};

// Whether ModLoader Proxy runs: one of its DLLs is a proxy next to the game, or EMF chain-loaded it. self: that
// DLL's file name.
bool modloader_proxy_runs(const GameFolderView& view, std::string* self = nullptr);

// How the core and EMF.exe look into the game folder (paths relative to it, UTF-8, '\' between folders).
struct FolderLook {
    std::function<std::string(const std::string&)> read_text;             // a small text file ("" when missing)
    std::function<bool(const std::string&)> exists;
    std::function<std::string(const std::string&)> tool;                  // the tool word of the DLL there
    std::function<std::vector<std::string>(const std::string&)> dlls_in;  // the *.dll file names in a folder
};

// When ModLoader Proxy runs (view.proxies, view.chained_tools), the DLLs its chainloader.ini starts go into
// view.loader_started with their tools. A path that leaves the game folder is listed, not read.
void add_loader_started(GameFolderView& view, const FolderLook& look);

std::vector<std::string> installed_report(const std::vector<KnownTool>& tools, const GameFolderView& view);

// File names DLL mods use to get into the game (the game imports them, or a DLL it imports forwards to them).
const std::vector<std::string>& proxy_dll_names();

}  // namespace emf::core
