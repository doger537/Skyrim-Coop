# TiltedConnect client callback overlay

`Client.cpp` derives from the pinned TiltedConnect client implementation. Its original license notice is preserved; the complete notice ships as `TiltedConnect-LICENSE.txt`.

The original static callback forwarded every process-wide GameNetworkingSockets status notification to whichever client's `Update()` was currently running. A delayed rejection from a previous probe could therefore close a different client's live connection during checkpoint recovery.

The overlay queues copied status notifications by native connection handle. Each client drains only its own inbox on its own `Update()` thread. Closing or destroying a client unregisters its handle before releasing it. No callback invokes another client's application code, and no mutex is held while dispatching application callbacks. Connection registration is covered by the same mutex as callback routing. As before, callers must serialize operations on each individual client.

`Libraries/xmake.lua` substitutes this file without changing the pinned submodule. When updating TiltedConnect, compare the original and overlay before updating the pin; retain upstream changes and this routing fix.
