// Tells Windows that sideSTEP can open STEP files (per user, no admin).
// Windows does not let a program make itself the default; after this runs the
// user picks sideSTEP once via "Open with > Always" or Settings > Default apps.
#pragma once

// Both return true on success. `sideSTEP.exe --register` / `--unregister`.
bool RegisterFileAssociation();
bool UnregisterFileAssociation();
