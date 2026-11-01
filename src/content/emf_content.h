/* emf_content.h: the C ABI of emf_content.dll, which builds the content of a game folder's EML mods.
   See src/content/README.md for the rules: load it only at the held start before any mod, never in EMF.exe, and
   unload it after the build. */
#ifndef EMF_CONTENT_H
#define EMF_CONTENT_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EMF_CONTENT_ABI 1
enum { EMF_CONTENT_OK = 0, EMF_CONTENT_MODS_FAILED = 1, EMF_CONTENT_RUN_FAILED = 2, EMF_CONTENT_BAD_REQUEST = 3,
       EMF_CONTENT_REPLY_TOO_SMALL = 4, EMF_CONTENT_PANIC = 5 };
/* level: 0 error, 1 warning, 2 info, 3 debug; line: one UTF-8 log line without a newline */
typedef void (*emf_content_log_fn)(void* ctx, int level, const char* line);
uint32_t emf_content_abi(void);
int32_t emf_content_build(const char* request_json, emf_content_log_fn log, void* log_ctx,
                          char* reply, size_t reply_cap, size_t* reply_len);

/* request_json (UTF-8):
     {"game_dir": "<folder holding enshrouded.kfc and mods\\>", "file_name": "enshrouded", "force": false,
      "server": null}
   file_name is optional (enshrouded or enshrouded_server when its .kfc or .exe is there, else the first .kfc or .exe);
   force rebuilds even when nothing changed; server is optional (null: EML decides from the file name). The run
   always patches, never exports and never registers runtime DLLs. log may be NULL; it is called only on the calling
   thread, during the call.

   reply: a NUL-terminated UTF-8 JSON
     {"code": n, "message": "...", "mods": [{"id": "...", "version": "..."}],
      "mod_errors": [{"id": "..." or null, "path": "...", "error": "..."}], "dlls": ["..."], "ms": n}
   *reply_len is always set to the reply's byte length with the NUL (reply_len must not be NULL: BAD_REQUEST).
   When reply_cap < *reply_len the call returns EMF_CONTENT_REPLY_TOO_SMALL and writes nothing; a retry with that
   size runs the build again, so pass a big buffer (64 KB). reply may be NULL when reply_cap is 0. */

#ifdef __cplusplus
}
#endif

#endif
