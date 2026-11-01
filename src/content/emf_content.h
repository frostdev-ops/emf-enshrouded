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
   always patches, never exports and never registers runtime DLLs. For tests only, "test_panic": "<message>" makes
   the call panic with that message (it gives EMF_CONTENT_PANIC).

   log may be NULL. It is called only on the calling thread, during the call. It must return normally: no C++
   exception and no longjmp may leave it.

   One call at a time: a call made while another runs (from another thread, or from inside the log callback) gives
   EMF_CONTENT_BAD_REQUEST at once. Call emf_content_build and FreeLibrary on the same thread.

   reply: a NUL-terminated UTF-8 JSON
     {"code": n, "message": "...", "mods": [{"id": "...", "version": "..."}],
      "mod_errors": [{"id": "..." or null, "path": "...", "error": "..."}], "dlls": ["..."], "ms": n}
   *reply_len is always set to the full reply's byte length with the NUL (reply_len must not be NULL: BAD_REQUEST).
   When reply_cap < *reply_len, the reply is cut to fit: it keeps the real code and ms, adds "truncated": true, has
   empty lists and as much of the message as fits, and the call still returns the real code. Only when not even that
   fits (about 100 bytes), or reply is NULL, does the call return EMF_CONTENT_REPLY_TOO_SMALL and write nothing; the
   build has run then and its result is unknown. Pass a big buffer (1 MB). */

#ifdef __cplusplus
}
#endif

#endif
