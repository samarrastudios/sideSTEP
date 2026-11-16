// Keeps STEP files previewable in File Explorer.
//
// Windows marks downloaded files as coming from the internet, and since late
// 2025 the preview pane refuses to run any preview handler on a marked file
// ("The file you are attempting to preview could harm your computer"). There
// is no setting for the preview pane alone, so sideSTEP removes the mark from
// the file types it opens (.step, .stp, .p21) and from nothing else.
//
// Plain Win32: this runs before, and instead of, the Qt application.
#pragma once
#include <string>

// Removes the mark from one file if it is a STEP file. Safe to call on anything.
void UnblockStepFile(const std::wstring& path);

// `sideSTEP.exe --watch`: unblocks the STEP files already on the fixed drives,
// then keeps watching and unblocks new ones as they arrive. One instance per
// user; a second one exits at once. Does not return while watching.
int RunUnblockWatcher();

// The user's switch for all of this. On: the watcher starts now and at every
// sign-in. Off: it stops now and no longer starts. `sideSTEP.exe --helper-on`
// and `--helper-off` do the same from an installer.
bool SetWatcherEnabled(bool enabled);
bool IsWatcherEnabled();
