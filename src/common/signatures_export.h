#pragma once
// EMF's engine signatures as a pattern file (pattern_file.h's format), for other loaders: after a game update one of us
// fixes a pattern and everyone can take it. docs/modding/emf_signatures.patterns is this text (a test keeps it current;
// `emf_sigcheck --export <file>` writes it, `emf_sigcheck --patterns <file> <exe>` checks it against an exe).
#include <string>

namespace emf {

// The whole file: a header (licence, how to check, the known builds with their exe sha256), then per signature a comment
// (required or optional, its RVA on each known build where it was checked, client only) and its pattern line. A
// signature that finds a function through a call site (SigDef::call_at) gets `; offset=<call_at + 1> ; rel32`.
std::string export_signature_patterns();

}  // namespace emf
