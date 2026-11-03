# Step 9 validation handoff

Status: implemented and locally validated on UE 5.8, 2026-11-02.
The design status deliberately remains short of Done until the two-PC Steam
procedure below is completed. No two-account joining or invite test is claimed.

## Independent-review fixes (2026-11-02)

All four findings are addressed:

1. **Identity binding:** PreLogin resolves the unique live connection by the
   engine-supplied transport address (ambiguous matches fail closed), not by the
   claimed login ID. Steam connections must be `USteamSocketsNetConnection`
   with a SteamID remote identity; IP/unknown transports under Steam fail closed.
   The remote Steam ID must equal the V1 STEAM login ID before ordinary admission
   checks. Login independently verifies the actual engine-provided UPlayer;
   InitNewPlayer binds the controller from that scoped login source before
   Super can initialize it. The new PC does not yet have a connection/local
   player association at that point. The round ledger's `RoundKey` requires the server's
   controller-lifetime binding to match the PlayerState ID on every access.
   Weak controller keys are pruned and cleared at shutdown; bindings persist
   across seamless travel. The local listen host is bound to the local OSS ID.
   **Null/-nosteam IDs remain unauthenticated** and retain their prior behavior.
   Do not expose the Null development path as an authenticated public service.

   Keeping a server-created binding, rather than looking up the current
   connection in Logout, is intentional: UE clears PC Player/NetConnection in
   OnNetCleanup *before* Destroyed/Logout captures gear and reconnect deadlines.
   The binding therefore also protects ledger access during disconnect cleanup.

2. **Private admission:** for Friends only / Invite only, the verified peer
   must occur in the current host-owned Steam lobby's
   `SteamMatchmaking()->GetLobbyMemberByIndex` list. Gameplay RegisteredPlayers
   is not used as authorization. PreLoginAsync retries membership propagation
   every 0.1 seconds for at most 5 seconds, then rejects with a join-through-
   Steam/retry error. During the wait it revalidates the same connection/ID;
   before completion it rechecks password, capacity and duplicate policy.
   Login checks membership again before player initialization.
   Pending callbacks use weak connections and are completed with failure on
   game-mode teardown/travel. Public sessions still permit verified direct
   Steam connections; passwords remain required for all visibility modes.
   This is an admission gate, not retroactive eviction of existing players or
   existing lobby members when visibility changes.

3. **Null privacy transitions:** public/private changes destroy and recreate
   only the Null named session, which actually stops/restarts its LAN beacon.
   The gameplay net driver/world is not restarted; live player registrations
   are restored. Failures retain repair intent and retry without leaving a
   running match. The extended session smoke checks actual beacon discovery
   **1 -> 0 -> 1** through Public -> Invite only -> Public.

4. **Publication retry:** attempted and successfully published snapshots are
   separate. Only a successful create/update commits PublishedSettings/signature.
   Failures leave the publication dirty and use 1, 2, 4, 8, 16, then capped
   30-second backoff, serialized with other session operations. Settings changed
   during an attempt are reconciled afterwards. Even rollback to the last
   successful value is retried after failure, because a backend update may have
   partially applied. Null recreation failures retain the repair intent whether
   the named session was removed or remains.

### Engine/API verification for this review

Read from `C:\Program Files\Epic Games\UE_5.8\Engine`:
- `Source/Runtime/Engine/Private/World.cpp`, NMT_Login (~7545–7556):
  assigns the client-supplied ID before PreLoginAsync, and supplies the transport
  address/completion delegate. `GameModeBase.h/.cpp` define its async contract.
- `Plugins/Runtime/Steam/SteamSockets/Source/SteamSockets/Private/`
  `SteamSocketsNetDriver.cpp::OnConnectionCreated`,
  `SteamSocket.cpp::GetPeerAddress`, `SteamSocketsNetConnection.cpp`:
  the accepted connection's RemoteAddr comes from Steam Networking
  `GetConnectionInfo().m_identityRemote`, independently of NMT_Login.
- `IPAddressSteamSockets.cpp::GetRawIp`: public `FInternetAddr::GetRawIp()`
  exposes SteamID identity type plus uint64 in big-endian byte order. No engine
  private-header inclusion, private socket access or engine patch was needed.
- `Plugins/Online/OnlineSubsystemSteam/Source/Private/`
  `OnlineSessionInterfaceSteam.cpp::IsPlayerInSession` uses gameplay
  registrations; it is not the Steam lobby membership gate. Steamworks
  `ISteamMatchmaking` membership/owner APIs provide that gate instead.
- `Source/Runtime/Engine/Private/PlayerController.cpp::OnNetCleanup` and
  `NetConnection.cpp::CleanUp/DestroyOwningActor`: explains the lifetime binding.
- `GameModeBase.cpp::Login` calls InitNewPlayer before
  `LevelActor.cpp::SpawnPlayActor` calls PC::SetPlayer. Login scopes its actual
  UPlayer using a weak reference and TGuardValue through synchronous Super::Login;
  binding must not use the unassociated new controller's GetNetConnection or
  IsLocalController to identify its peer.
- `Plugins/Online/OnlineSubsystemNull/Source/Private/OnlineSessionInterfaceNull.cpp`:
  UpdateSession only copies settings; IsSessionJoinable treats bIsLANMatch as
  advertised. DestroySession removes the named session and runs UpdateLANStatus;
  CreateSession also runs UpdateLANStatus. This is why a plain flag update was
  insufficient.
- Added private `SteamSockets`/`Sockets` dependencies and the engine Steamworks
  third-party dependency for the public transport type/address and lobby API.
  Existing plugins were reused; no additional plugin was enabled.

Review tests added: `BrGame.Session.IdentityBinding`, `.PrivateAdmission`,
`.PublicationRetry`. They cover victim-ID/random-ID substitution, missing or
wrong-provider identities, Null policy, private membership delay/timeout,
public policy, non-commit on failure, same-settings retry, partial-failure
rollback repair, backoff ceiling and success reset. These are deterministic
policy tests, not a crafted network-client integration test.

## Implementation and choices

- OSSv1 `OnlineSubsystemSteam`, not OSSv2: the installed 5.8 Steam session
  implementation explicitly supports lobby/presence sessions, overlay callbacks
  and startup `+connect_lobby` invites. SteamSockets supplies the listen driver
  and relay network. Engine `IsAvailable()` driver selection falls back to
  `OnlineSubsystemUtils.IpNetDriver` when Steam is unavailable.
- `UBrSessionSubsystem` owns create/find/join/update/destroy, serializes named
  session operations, retains selected results through menu travel, and waits
  for destruction before switching games. Delegates and the one-second core
  ticker are removed on teardown. Normal Quit waits for session destruction;
  abrupt OS/process termination can only have best-effort cleanup.
- Advertised metadata: BrGame identifier, protocol 9, host/game name, count,
  host maximum, mode, lock flag and in-progress flag. The engine's default
  build ID is retained: Null discards mismatching IDs before our filters run.
  Update reconciles changed metadata/visibility/capacity across seamless travel,
  results, new lobby, arrivals and departures. Late joining stays enabled.
- Visibility maps to UE's actual `BuildLobbyType`: public, friends-only, private
  invite-only. Presence updates derive Steam's `connect` key from the named
  session and clear it for invite-only sessions or after destruction.
- Steam authentication packet handler is enabled; it disables itself on Null.
  Login IDs are additionally bound to the SteamSockets peer as described above;
  a successful ticket check alone is not treated as proof of the claimed ID.
  Existing server admission checks still enforce the host maximum, including
  races caught in PostLogin. No player-selected developer values were added.
- Invites do NOT bypass passwords. Browser/invite joins prompt before joining.
  IP join has its own optional password field. The server checks the credential
  in PreLogin; an RPC's protection flag cannot disable it. Only the server keeps
  the password, only the host sees the credential controls, and no secret is
  advertised or saved. Changing a password does not kick connected players.
- Literal passwords are never placed in URLs or project logs. A domain-prefixed
  SHA-1 bearer proof is passed as `BrProof`, checked against a server-computed
  proof using fixed-length comparison. This avoids plaintext travel logging,
  but is NOT a PAKE or a password-storage KDF: engine/network diagnostic logs
  can contain a reusable credential proof. Treat them as sensitive, choose a
  unique game password, and do not use an account password. Null/IP is not an
  encrypted transport. Do not distribute raw connection logs.
- Steam lobbies impose a backend limit of 250 members. Host 0 remains unlimited
  in gameplay admission, advertised as 250 on Steam / 1,000,000 on Null.
  A finite Steam capacity above the backend limit can fail creation/update;
  use a capacity supported by Steam. This is not a new game admission cap.
- `-nosteam` is the supported development switch. Steam's OSS module implements
  it; default OSS initialization falls back to Null. Local scripts explicitly
  pass it. Non-public Null sessions stop LAN advertising; IP knowledge is not
  friends/invite authorization, so use a password if private LAN access matters.
- Errors go to the flow UI. Pending connection errors with a null World are now
  scoped by this game instance's pending driver, so password rejection and
  unreachable-address failures clean up the joined session too. Unknown
  in-flight operation timeouts return to the menu without starting overlapping
  OSS operations; restart if Steam never completes the pending operation.

## Changed paths

New:
- `Source/BrGame/Core/BrSessionSubsystem.h`, `.cpp`
- `Source/BrGame/Core/BrSessionRules.h`
- `Source/BrGame/Tests/BrSessionTest.cpp`
- `Scripts/RunSessionSmoke.ps1`, `Scripts/RunSteamHostCheck.ps1`
- `Scripts/Step9Validation.md`

Changed:
- `BrGame.uproject`
- `Source/BrGame/BrGame.Build.cs`
- `Config/DefaultEngine.ini`
- `Source/BrGame/Core/BrMatchSettings.h`
- `Source/BrGame/Core/BrGameInstance.h`, `.cpp`
- `Source/BrGame/Core/BrFlowGameModes.cpp`
- `Source/BrGame/Core/BrGameMode.h`, `.cpp`
- `Source/BrGame/Core/BrFlowWidget.h`, `.cpp`
- `Scripts/RunLocalMultiplayer.ps1`, `RunFlowSmoke.ps1`, `RunFogSmoke.ps1`
- `GameDescription.md` (step 9 / Hosting / Flow only)

No template classes or binary assets were edited. Generated Binaries,
Intermediate and Saved changes are build/editor outputs, not authored changes
(this repository tracks some of those outputs).

## Actual commands and results

From `C:\Users\oiste\Projects\Unreal\BrGame`, with editor closed:

```powershell
Get-Process UnrealEditor,LiveCodingConsole -ErrorAction SilentlyContinue
$UE = "C:\Program Files\Epic Games\UE_5.8"
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGameEditor Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Build\BatchFiles\Build.bat" BrGame Win64 Development "-Project=$PWD\BrGame.uproject" -WaitMutex
& "$UE\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" "$PWD\BrGame.uproject" '-ExecCmds=Automation RunTests BrGame; Quit' -unattended -nullrhi -nosound -log -nosteam
& Scripts/RunSessionSmoke.ps1
& Scripts/RunSteamHostCheck.ps1
& Scripts/RunFlowSmoke.ps1
& Scripts/RunFlowSmoke.ps1 -Grouped
& Scripts/RunFlowSmoke.ps1 -Zone
& Scripts/RunFlowSmoke.ps1 -DisconnectGrace
& Scripts/RunFogSmoke.ps1
& Scripts/RunFogSmoke.ps1 -Grouped
```

- Full editor/UHT and standalone game builds succeeded, no source warnings.
  Reflected layout changes were built with editor closed, not Live Coding.
- Automation: **43 successes**, `TEST COMPLETE. EXIT CODE: 0` (37 original plus
  six `BrGame.Session.*` tests). The engine
  startup SDK probe reports missing non-Windows SDKs; Win64 is valid and tests
  exit successfully.
- Null session smoke PASS: actual LAN beacon search finds one compatible host;
  wrong credential rejected in server PreLogin; rejected client's named session
  destroyed; menu rebuilt after rejection. Correct credential joins lobby,
  then Leave destroys the session and rebuilds the menu. Exactly two host
  admissions (host plus correct client); no admission for wrong password.
  Extended review run also passed real LAN browser results **1 -> 0 -> 1** while
  the host switches Public -> Invite only -> Public; no world travel is used
  for those visibility transitions.
- Required flow default/grouped/zone/disconnect-grace and fog smokes PASS.
  Fog confirms enemy client actor presence 0 -> 1 -> 0; grouped fog also passed
  teammate presence 1 -> 1 -> 1.
- Steam client WAS running. Standalone `-game` check PASS:
  `[AppId: 480] Client API initialized 1`, Game Server API initialized 1,
  `SteamSocketsNetDriver_0 started listening on 7777`,
  BrGame/protocol metadata published, lock flag updated, create/update/destroy
  callbacks all successful, and process exited normally. This is not proof of
  a remote relay connection.
- Logs: `Saved/Logs/BrGame.log`, `Saved/Logs/MP/Session/`,
  `Saved/Logs/MP/SteamHost.log`, `MP/Flow/`, `MP/FlowGrouped/`, `MP/Zone/`,
  `MP/FlowDisconnect/`, `MP/Fog/`.
  Grouped fog logs are in `MP/FogGrouped/`.

The editor and standalone builds and all commands above were rerun after the
review fixes, with editor/LiveCoding checks before building. No source warnings.
Steam was running and the final single-instance check passed again. Tests do
not claim an actual Steam service update-failure/backoff injection or a remote
Steam identity/privacy attack; the retry state is tested deterministically.
On the final rerun, the first Steam attempt initialized/authenticated/listened
successfully but Steam's CreateLobby timed out (`k_EResultTimeout`, result 16;
also an SDR configuration HTTP 504 followed by successful recovery).
The game cleaned up and returned to the menu. A subsequent standalone attempt
passed create/update/destroy; no source/config workaround was applied.

Earlier iterations: platform `GetSHA256Signature` compiled but asserted at
runtime on Windows; replaced with Core's implemented SHA-1 bearer transform.
Initial LAN discovery failed because a custom BuildUniqueId was rejected by
Null before the callback; retaining the engine default fixed it. The rejection
smoke then exposed the existing null-World network-failure handling gap; the
pending-driver scoping fix passed. One immediate post-smoke Steam-check launch
correctly refused while smoke-owned processes were still terminating; new
session smoke cleanup now waits for those exact PIDs to exit. No pre-existing
editor process was killed.

## Manual Steam procedure (still required)

Use two PCs, two different logged-in Steam accounts, matching game/engine builds
and compatible protocols. Steam overlay must be enabled. Do NOT use PIE or run
multiple Steam users/processes on one account as a substitute.

On each PC (adjust project path there):

```powershell
& "C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe" "C:\Users\oiste\Projects\Unreal\BrGame\BrGame.uproject" -game -windowed -log
```

Alternatively use matching packaged Development builds, starting their BrGame
executable. The standalone game target was linked here, but packaging/cooking
and executing a packaged build were not performed. The engine handles
`steam_appid.txt` in non-shipping development; do not check it into source.
Changing `SteamDevAppId` in DefaultEngine.ini changes the development App ID;
a production App ID also needs its proper Steamworks app/launch setup.

1. **Public browser:** A sets a name and Hosts. B refreshes. Only compatible
   BrGame games should appear, not unrelated Spacewar games. Confirm name,
   count/max, mode, lock, ping (or unavailable). Join. Confirm count changes.
2. **Live settings:** A sets a small max, changes grouped mode and visibility.
   Refresh B's browser after leaving. Check public discovery, friends-only
   discovery/access using a friend versus nonfriend, and invite-only absence
   from public search / friends-list Join Game. Steam controls lobby membership.
3. **Password:** A sets a unique password. B joins by browser; prompt appears.
   Wrong/blank returns to start page with “Incorrect or missing server password.”
   Correct admits. Repeat through an overlay invite: it also requires password.
   Remove password and verify lock/prompt disappear after refresh.
4. **Overlay:** A presses Shift+Tab and invites B. Accept from the start page,
   then repeat while B is in another hosted/joined session. Old session must
   leave before the new join/prompt. Test a stale and an incompatible invite.
5. **Friends list:** for public/friends-only games, B selects Join Game on A in
   Steam. It must route through the same compatibility/password checks.
6. **Cold start:** close B's game and accept an invite. UE supports
   `+connect_lobby <lobby-id>` and holds startup invites until our delegate binds.
   Steam must actually launch BrGame with that command line. **App ID 480 is
   Spacewar: stock Steam launch routing may launch Spacewar instead.** Reliable
   cold-start activation requires an App ID/Steam launch configuration pointing
   at BrGame; it cannot be proven just by setting SteamDevAppId. If testing 480,
   inspect the launch target/arguments; manually starting BrGame with the
   forwarded `+connect_lobby` arguments tests the engine startup path but does
   not prove Steam launcher integration. No registry/Steam installation edits
   were made by this change.
7. **Match/late join:** A starts. B refreshes from another instance/PC and checks
   In progress, then joins late with password. Finish the round and check lobby/
   results state updates and new-round joining; no new session should be needed.
8. **Capacity:** finite max includes the host; reject the next player. Test 0
   accepting additional players (do not attempt backend limits as a load test).
9. **Leaving/failure:** B leaves and rejoins. A leaves/quits: B returns to start
   page with host-left/connection message and old game disappears. Rehost and
   rejoin. Test unavailable host, lost Steam login and blocked network/relay;
   verify useful errors and no indefinite success-shaped state.
10. **Dev fallback:** repeat local Host/browser/IP Join with `-nosteam`, or use
    `Scripts/RunLocalMultiplayer.ps1 -Clients 1 -Menu`. No Steam dependency
    should remain. Closing Steam entirely and checking automatic fallback is
    still a manual case (Steam was not stopped here).
11. **Security regression, two PCs:** with a development modified client,
     authenticate as B but send A's ID (and then another ID) in NMT_Login.
     Both must fail with the identity mismatch message before admission/ledger
     restoration; A's penalties/gear must remain intact on reconnect. Test normal
     Steam disconnect/reconnect too, including gear/deaths and grace expiry,
     because controller binding intentionally survives net-connection cleanup.
     For each private visibility, remove the password and attempt a direct
     SteamSockets connection from a nonmember without JoinSession: expect
     rejection after at most the membership wait. A legitimate invited/friend
     lobby join must still work, including delayed membership propagation.
     Close a pending client and make the host travel during a pending join to
     confirm safe cancellation; repeat after results/seamless lobby travel.

No Blueprint/map/animation/UI asset work or editor asset regeneration is
required. Remaining coverage: real two-account Steam joins, invite/launcher
activation, remote friends/privacy enforcement, NAT/SDR transport, visual UI
usability, packaged execution and high-player-count/packet-loss stress.
