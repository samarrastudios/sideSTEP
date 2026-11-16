// Shared by the thumbnail provider and the preview handler inside the shell DLL.
#pragma once
#include <windows.h>

#include <string>

#include "thumb_core.h"

extern HINSTANCE g_module;
extern LONG g_refs;  // live objects, windows and worker threads; the DLL stays loaded while > 0

// Opt-in diagnostics, see thumbnail_provider.cpp.
void DebugLog(const char* what, HRESULT hr = S_OK);

// LoadStep / RenderStepThumbnail guarded against hardware exceptions (access
// violations inside the geometry kernel), so a bad file cannot take the host
// process down. Return false on any failure.
bool SafeLoadStep(const char* data, size_t size, const stepcore::LoadOptions& opt, stepcore::Mesh& out, std::string& error);
bool SafeRenderThumbnail(const char* data, size_t size, int px, stepcore::Image& out, std::string& error);

// {8F1C0C46-5E0B-4D3A-9B56-6C7F3F2A51D4}
extern const CLSID CLSID_SideStepPreview;
HRESULT CreatePreviewHandler(REFIID riid, void** ppv);
