# Local PvE death menu

Both community clients can open the original role selection screen when a dead local player presses their configured in-game menu action and the game presents its Leave Match confirmation. The original widget handles Back first. Role selection opens once its prompt has left the stack and the controller still permits role selection.

If no prompt appears, the same local death action now opens role selection after the observed press and release on an otherwise empty screen. A player who releases during cooldown can wait up to three seconds for the original permission; another menu, another press or respawn cancels that request. The OSS retail session confirmed the local PvE UI scope; it did not record the prompt stack at the instant of the death key.

If the player presses during the deployment cooldown, the same Leave Match prompt may remain open while the game finishes enabling role selection. The client preserves the original key press for up to three seconds. Another press, dismissal, a different prompt or respawn cancels that request. The dialog remains untouched until role selection is actually available.

The launcher supplies `-BrtLocalPveUi` for a local PvE session. The client also requires its own completed loopback travel command, the corresponding new world and server connection, the same local player, a running Easy, Normal or Hard PvE round, and the player's replicated Death status. The previous observation must show clear menu stacks. The new prompt must match the original type, owner, localized title, description and two options. A replaced dialog, open menu, different controller, respawn or loss of selection permission cancels the gesture.

For the split community source, apply `split.patch` and `sdk-split.patch`. Copy `DeathMenu.hpp` and `DeathMenu.sdk.hpp` beside `ProjectReboundMainDLL/Client/AutoConnect.cpp` before building. The patch calls the existing game-thread command pump.

For v0.8.4, apply `v084.patch` and `sdk-v084.patch`. Copy the same two headers plus `DeathMenuTravel.hpp` and `DeathMenu.v084.hpp` into `Payload/Utility`. The existing initial connection and reconnect branches publish the identity of their completed travel operation. The game's character UI tick checks the room's own local player, including when another character supplies the tick. Opening the selection screen requires at least one character tick after the original prompt closes. Both project patches add the original Blueprint's missing Back implementation to the build.

The verifier compiles the original and patched translation units against the full game SDK. It runs the calls extracted from each version against controlled input, localized text, game state and CommonUI widget stacks. It records each case and both source digests under the selected output directory. The recorded player session documents the original symptom. A fresh retail playthrough is still needed to verify the patched screen in the running game.

```powershell
.\verify.ps1 -CommunityRoot D:\path\to\rebound-source -OutDir D:\path\to\verification
.\verify-v084.ps1 -CommunityRoot D:\path\to\v084-source -OutDir D:\path\to\verification
.\verify-sdk-link.ps1 -Layout split -CommunityRoot D:\path\to\rebound-source -OutDir D:\path\to\verification
.\verify-sdk-link.ps1 -Layout v084 -CommunityRoot D:\path\to\v084-source -OutDir D:\path\to\verification
```
