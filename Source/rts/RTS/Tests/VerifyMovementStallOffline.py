"""No Unreal process/module is started or built. Outputs live only in PI_SCRATCH_DIR.
Runs the real progress header/tests with a minimal FVector adapter, plus source wiring checks.
"""
import os
from pathlib import Path
import re
import subprocess
import tempfile

RTS = Path(__file__).resolve().parents[1]
unit = (RTS / "Units/RTSUnit.cpp").read_text(encoding="utf-8-sig")
traffic = (RTS / "Units/RTSUnitTraffic.cpp").read_text(encoding="utf-8-sig")
header = (RTS / "Units/RTSUnit.h").read_text(encoding="utf-8-sig")
progress = (RTS / "Orders/RTSMovementProgress.h").read_text(encoding="utf-8-sig")

checks = [
    ("all three committed non-lane route sites rebase only on accepted requests", unit.count("{ BeginFormationRoute(); }") == 3),
    ("formation samples the full waypoint route", "FormationProgress.Observe(CurrentOrder.TargetLocation, GetRemainingFormationDistance(), Now)" in unit),
    ("traffic samples the full waypoint route too", "MoveGroup.IsValid() ? GetRemainingFormationDistance() : GetRemainingPathDistance()" in traffic),
    ("new route rebases both observers", "FormationProgress.BeginRoute(CurrentOrder.TargetLocation, Remaining, Now);" in traffic and "TrafficProgress.BeginRoute(CurrentOrder.TargetLocation, Remaining, Now);" in traffic),
    ("same route retries preserve recovery deadline", "if (bSameRoute) { return; }" in traffic and traffic.index("if (bSameRoute) { return; }") < traffic.index("FormationProgress.BeginRoute(")),
    ("leaving the group clears route identity", "FormationRouteSnapshot.Reset();" in unit),
    ("phase 3 is not a sidestep/wait", "bool IsTrafficSidesteppingOrWaiting() const { return YieldPhase == 1 || YieldPhase == 2; }" in header),
    ("tick uses the phase-specific gate", "if (IsTrafficSidesteppingOrWaiting())" in unit),
    ("recovery uses the phase-specific gate", "if (IsTrafficSidesteppingOrWaiting()) { return; }" in traffic),
    ("blocker accepts a returning requester", "|| Requester->IsTrafficSidesteppingOrWaiting()) { return false; }" in traffic),
    ("failed-path diagnostics include start navigation", "startNav=%s navBuilding=%d mode=%d speed=%.1f" in unit),
    ("bounded original group timeout remains", "constexpr float GroupMoveTimeout = 90.0f;" in unit),
    ("no new collision exception or teleport in rebase", not re.search(r"SetActorLocation|MoveComponent|IgnoreActorWhenMoving|SetCapsule|SetCollision", traffic.split("void ARTSUnit::BeginFormationRoute()", 1)[1].split("void ARTSUnit::EndCongestionRecovery()", 1)[0])),
    ("no stray edit operation text", not re.search(r"^(PUT|CUT) ", unit + traffic + header, re.M)),
]
for name, passed in checks:
    if not passed:
        raise AssertionError(name)
print(f"Movement source wiring: {len(checks)} checks passed", flush=True)

vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
vs = subprocess.check_output([str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"], text=True).strip()
vcvars = Path(vs) / "VC/Auxiliary/Build/vcvars64.bat"
env_output = subprocess.check_output(f'cmd /u /d /s /c "call "{vcvars}" >nul && set"').decode("utf-16-le")
env = os.environ.copy()
for line in env_output.splitlines():
    if "=" in line and not line.startswith("="):
        key, value = line.split("=", 1)
        env[key.upper()] = value

scratch = os.environ["PI_SCRATCH_DIR"]
with tempfile.TemporaryDirectory(prefix="movement-stall-", dir=scratch) as temp:
    work = Path(temp)
    # Only Equals and ZeroVector are needed by the production pure-data header.
    (work / "CoreMinimal.h").write_text('''#pragma once
#include <cmath>
struct FVector {
    double X, Y, Z;
    FVector(double x=0, double y=0, double z=0): X(x), Y(y), Z(z) {}
    bool Equals(const FVector& b, double t) const {
        return std::abs(X-b.X)<=t && std::abs(Y-b.Y)<=t && std::abs(Z-b.Z)<=t;
    }
    static const FVector ZeroVector;
};
inline const FVector FVector::ZeroVector{};
''', encoding="utf-8")
    compiler = next((Path(vs) / "VC/Tools/MSVC").glob("*/bin/Hostx64/x64/cl.exe"))
    test = RTS / "Tests/RTSMovementStallProgressTests.cpp"

    def build_and_run(name):
        exe = work / (name + ".exe")
        subprocess.run([str(compiler), "/nologo", "/EHsc", "/std:c++17", "/W4", "/WX", "/DRTS_MOVEMENT_STALL_STANDALONE", f"/I{work}", f"/I{RTS}", str(test), f"/Fo{work / (name + '.obj')}", f"/Fe{exe}"], cwd=work, env=env, check=True)
        return subprocess.run([str(exe)], cwd=work, env=env).returncode

    assert build_and_run("progress") == 0
    # Negative control: omission of a route rebase must fail the very same checks.
    (work / "Orders").mkdir()
    assignment = "Goal = InGoal; BestRemaining = Remaining; LastProgressAt = Now;"
    mutated = progress.replace(assignment, "(void)InGoal; (void)Remaining; (void)Now;", 1)
    assert mutated != progress
    (work / "Orders/RTSMovementProgress.h").write_text(mutated, encoding="utf-8")
    assert build_and_run("missing_rebase") != 0
    print("Negative control: missing route rebase rejected (expected test failure)", flush=True)
print("Offline movement checks passed; this does not compile or simulate Unreal movement.")
