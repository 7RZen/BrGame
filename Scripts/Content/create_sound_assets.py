"""UE 5.8 commandlet. Deterministic original placeholders; no external recordings.
Re-runnable. WAV sources are retained under Scripts/Content/Audio; cooked SoundWaves
and class/mix assets are under /Game/BrGame/Audio (already always-cooked).

Optional filter (generate/import ONLY the named SW_ assets; touches no shared asset):
    BR_SOUND_ONLY=Reload_Pistol,DryFire
When BR_SOUND_ONLY is set the shared concurrency/class/mix setup is skipped entirely, so
existing binaries are not re-saved. Spec keys are the names without the "SW_" prefix.
"""
import math
import os
import random
import struct
import wave
from pathlib import Path
import unreal

ROOT = "/Game/BrGame/Audio"
RATE = 22050
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
SOURCE = Path(unreal.Paths.project_dir()) / "Scripts" / "Content" / "Audio"
ONLY = {k.strip() for k in os.environ.get("BR_SOUND_ONLY", "").split(",") if k.strip()}

# Mechanical events for reload/dry-fire clicks: (time s, ring Hz, amplitude, decay /s).
MECH_EVENTS = {
    "reload_pistol": [(0.04, 1900, 0.55, 70), (0.36, 1350, 0.6, 60), (0.56, 2300, 0.5, 80)],
    "reload_rifle": [(0.08, 1300, 0.6, 45), (0.70, 1100, 0.7, 40), (0.70, 180, 0.5, 14),
                     (1.36, 1750, 0.6, 60)],
    "reload_mg": [(0.12, 300, 0.8, 12), (0.95, 900, 0.55, 45), (1.70, 1600, 0.25, 90),
                  (1.84, 1500, 0.22, 90), (1.97, 1700, 0.25, 90), (2.10, 1450, 0.22, 90),
                  (2.75, 520, 0.8, 16)],
    "dry": [(0.0, 520, 0.32, 90)],
}


def save(obj):
    if not obj or not unreal.EditorAssetLibrary.save_loaded_asset(obj, only_if_is_dirty=False):
        raise RuntimeError("Audio asset save failed: " + str(obj))


def asset(name, cls, factory):
    path = ROOT + "/" + name
    obj = unreal.load_asset(path) if unreal.EditorAssetLibrary.does_asset_exist(path) else TOOLS.create_asset(name, ROOT, cls, factory)
    if not obj or obj.get_class() != cls.static_class():
        raise RuntimeError("Wrong/missing audio asset: " + path)
    return obj


def samples(name, duration, seed, kind, frequency):
    rng = random.Random(seed)
    phase = 0.0
    low = 0.0
    for i in range(int(duration * RATE)):
        t = i / RATE
        noise = rng.uniform(-1, 1)
        low = low * 0.92 + noise * 0.08
        fade = min(1, t / 0.003) * min(1, (duration - t) / 0.02)
        if kind == "fire":
            value = (0.6 * noise * math.exp(-t * 65) +
                     0.45 * math.sin(2 * math.pi * frequency * t) * math.exp(-t * 22))
        elif kind == "boom":
            value = (0.7 * low + 0.4 * math.sin(2 * math.pi * (65 * t - 12 * t * t))) * math.exp(-t * 3.5)
        elif kind == "scream":
            # Original vowel-like harmonic glide. NOT the Wilhelm scream.
            f = 430 - 220 * t / duration + 15 * math.sin(2 * math.pi * 6 * t)
            phase += 2 * math.pi * f / RATE
            value = sum(math.sin(h * phase) / h *
                        (0.25 + math.exp(-((h * f - 900) / 330) ** 2) +
                         0.6 * math.exp(-((h * f - 1700) / 400) ** 2)) for h in range(1, 12))
            value *= 0.35 * math.sin(math.pi * t / duration) ** 0.5
        elif kind == "step":
            value = (0.45 * noise * math.exp(-t * 60) +
                     0.5 * math.sin(2 * math.pi * frequency * t) * math.exp(-t * 45))
        elif kind == "shatter":
            # Plate crack: sharp broadband transient plus fast-decaying inharmonic partials (dissonant, glassy/metallic).
            partials = [(1.0, 0.5), (1.47, 0.35), (2.13, 0.25), (2.9, 0.18)]
            tone = sum(a * math.sin(2 * math.pi * frequency * r * t) for r, a in partials)
            value = (0.7 * noise * math.exp(-t * 110) + 0.35 * tone * math.exp(-t * 26) +
                     0.15 * low * math.exp(-t * 8))
        elif kind == "slide":
            value = (noise * 0.24 + low * 1.8) * math.sin(math.pi * t / duration) ** 0.6
        elif kind in MECH_EVENTS:
            # Metal clicks: each event is a short noise transient plus a decaying ring.
            value = 0.0
            for t0, ring, amp, decay in MECH_EVENTS[kind]:
                if t >= t0:
                    dt = t - t0
                    value += amp * (0.5 * noise * math.exp(-dt * 260) +
                                    math.sin(2 * math.pi * ring * dt) * math.exp(-dt * decay))
            value *= 0.7
        elif kind == "grunt":
            # Short, low, vowel-like placeholder (original harmonic glide, not a recording).
            f = 200 - 70 * t / duration + 10 * math.sin(2 * math.pi * 9 * t)
            phase += 2 * math.pi * f / RATE
            value = sum(math.sin(h * phase) / h *
                        (0.2 + math.exp(-((h * f - 650) / 260) ** 2) +
                         0.5 * math.exp(-((h * f - 1150) / 320) ** 2)) for h in range(1, 10))
            value = 0.32 * value * math.sin(math.pi * t / duration) ** 0.5 + 0.05 * noise * math.exp(-t * 30)
        elif kind == "thud":
            # Dull, damped impact: low body with a little lowpassed noise; no bright partials.
            value = (0.55 * math.sin(2 * math.pi * frequency * t) * math.exp(-t * 30) +
                     0.35 * low * math.exp(-t * 22))
        else:
            value = (math.sin(2 * math.pi * frequency * t) +
                     0.4 * math.sin(2 * math.pi * frequency * 1.5 * t)) * 0.4 * math.exp(-t * 18)
        yield int(max(-0.92, min(0.92, value * fade)) * 32767)


def main():
    SOURCE.mkdir(parents=True, exist_ok=True)
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
    legacy = ["Fire", "Explosion", "Downed", "Footstep", "Pickup"]
    voices = {}
    for category in legacy:
        policy = settings.get_editor_property(category + "Voice")
        if ONLY:
            # Filtered run: read the shared concurrency asset, never re-save it.
            concurrency = unreal.load_asset(ROOT + "/SCN_" + category)
            if not isinstance(concurrency, unreal.SoundConcurrency):
                raise RuntimeError("Run the full script once first; missing SCN_" + category)
        else:
            concurrency = asset("SCN_" + category, unreal.SoundConcurrency, unreal.SoundConcurrencyFactory())
            rules = concurrency.get_editor_property("concurrency")
            rules.set_editor_property("max_count", policy.get_editor_property("MaxCount"))
            rules.set_editor_property("resolution_rule", policy.get_editor_property("ResolutionRule"))
            rules.set_editor_property("limit_to_owner", False)
            rules.set_editor_property("voice_steal_release_time", 0.02)
            concurrency.set_editor_property("concurrency", rules)
            save(concurrency)
        voices[category] = (concurrency, policy.get_editor_property("Priority"))
    # Issue #1 categories: no SCN_ asset (Concurrency=None in config; C++ builds runtime budgets).
    # Sounds get priority from config only, so they carry an empty concurrency_set.
    for category in ["Reload", "DryFire", "Pain"]:
        voices[category] = (None, settings.get_editor_property(category + "Voice").get_editor_property("Priority"))
    if ONLY:
        effects = unreal.load_asset(ROOT + "/SC_Effects")
        if not isinstance(effects, unreal.SoundClass):
            raise RuntimeError("Run the full script once first; missing SC_Effects")
    else:
        master = asset("SC_Master", unreal.SoundClass, unreal.SoundClassFactory())
        effects = asset("SC_Effects", unreal.SoundClass, unreal.SoundClassFactory())
        master.set_editor_property("child_classes", [effects])
        effects.set_editor_property("parent_class", master)
        save(master)
        save(effects)
        mix = asset("SM_Volume", unreal.SoundMix, unreal.SoundMixFactory())
        mix.set_editor_property("duration", -1.0)
        save(mix)
    specs = [
        ("Fire_Pistol", 0.18, "fire", 220), ("Fire_AssaultRifle", 0.23, "fire", 150),
        ("Fire_MachineGun", 0.28, "fire", 95), ("GrenadeExplosion", 1.4, "boom", 65),
        ("DownedScream", 1.0, "scream", 430),
        ("Pickup_Weapon", 0.25, "chime", 530), ("Pickup_Ammo", 0.12, "chime", 1000),
        ("Pickup_Health", 0.35, "chime", 800), ("Pickup_Grenade", 0.18, "chime", 380),
    ]
    for n in range(3):
        specs += [("Step_Run" + str(n), 0.14, "step", 95 + n * 15),
                  ("Step_Soft" + str(n), 0.10, "step", 160 + n * 20)]
    specs += [("Slide", settings.get_editor_property("SlideDuration"), "slide", 0)]
    # Appended last so earlier seeds (index-based) are unchanged. Plays on PickupVoice (category "Pickup").
    specs += [("ArmorBreak", 0.45, "shatter", 1250)]
    # Issue #1 additions (appended; earlier seeds unchanged). Durations stay inside the weapon ReloadTime.
    specs += [("Reload_Pistol", 1.2, "reload_pistol", 0), ("Reload_AssaultRifle", 1.8, "reload_rifle", 0),
              ("Reload_MachineGun", 3.4, "reload_mg", 0), ("DryFire", 0.12, "dry", 0),
              ("Pain", 0.35, "grunt", 0), ("PainArmor", 0.15, "thud", 140)]
    known = {key for key, _, _, _ in specs}
    unknown = ONLY - known
    if unknown:
        raise RuntimeError("BR_SOUND_ONLY has unknown keys: " + ",".join(sorted(unknown)))
    for seed, (key, duration, kind, frequency) in enumerate(specs):
        if ONLY and key not in ONLY:
            continue
        name = "SW_" + key
        file = SOURCE / (name + ".wav")
        with wave.open(str(file), "wb") as out:
            out.setnchannels(1)
            out.setsampwidth(2)
            out.setframerate(RATE)
            out.writeframes(b"".join(struct.pack("<h", s) for s in samples(name, duration, 4811 + seed, kind, frequency)))
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(file))
        task.set_editor_property("destination_path", ROOT)
        task.set_editor_property("destination_name", name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        TOOLS.import_asset_tasks([task])
        sound = unreal.load_asset(ROOT + "/" + name)
        if not isinstance(sound, unreal.SoundWave):
            raise RuntimeError("WAV import failed: " + name)
        sound.set_editor_property("sound_class_object", effects)
        sound.set_editor_property("looping", False)
        category = ("Fire" if kind == "fire" else "Explosion" if kind == "boom" else
                    "Downed" if kind == "scream" else "Footstep" if kind in ["step", "slide"] else
                    "Reload" if kind.startswith("reload") else "DryFire" if kind == "dry" else
                    "Pain" if key.startswith("Pain") else "Pickup")
        concurrency, priority = voices[category]
        sound.set_editor_property("override_concurrency", False)
        sound.set_editor_property("concurrency_set", {concurrency} if concurrency else set())
        sound.set_editor_property("priority", priority)
        sound.set_editor_property("bypass_volume_scale_for_priority", category in ["Downed", "Footstep", "DryFire", "Pain"])
        save(sound)
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/BrGame.BrDeveloperSettings"))
    for prop in ["RunFootsteps", "SoftFootsteps"]:
        steps = settings.get_editor_property(prop)
        if len(steps) != 3 or any(not step for step in steps):
            raise RuntimeError("Expected three valid configured " + prop + "; check DefaultGame.ini array syntax")
    for prop in ["DownedScream", "DefaultPickupSound", "MasterSoundClass", "EffectsSoundClass", "VolumeSoundMix",
                 "DryFireSound", "PainSound", "PainArmorSound"]:
        if not settings.get_editor_property(prop):
            raise RuntimeError("Audio setting reference missing: " + prop)
    for category in legacy:
        if not settings.get_editor_property(category + "Voice").get_editor_property("Concurrency"):
            raise RuntimeError("Missing configured category concurrency: " + category)
    if ONLY:
        unreal.log("Filtered Br audio import complete: " + ",".join(sorted(ONLY)))
    else:
        unreal.log("Br audio assets verified: deterministic mono SoundWaves, Master/Effects hierarchy and volume mix")


main()
