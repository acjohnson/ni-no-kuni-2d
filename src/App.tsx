import React, { useCallback, useEffect, useRef, useState } from 'react';
import {
  ChevronRight,
  Compass,
  Film,
  Heart,
  Play,
  RotateCcw,
  Sliders,
  Swords,
  Volume2,
  VolumeX,
} from 'lucide-react';
import { BattleHudOverlay } from './components/BattleHudOverlay';
import { ControlsModal } from './components/ControlsModal';
import { EngineWorkbench } from './components/EngineWorkbench';
import { PortraitFrame } from './components/PortraitFrame';
import {
  CutsceneOpExport,
  EncounterPreset,
  EngineTuning,
  FamiliarSlot,
  UiSnapshot,
} from './core/types';
import { RetroInputAdapter } from './host/inputAdapter';
import { PixelAtlas } from './host/pixelAtlas';
import { SoftwarePpuRenderer } from './host/ppuRenderer';
import { RetroSynthAudio } from './host/synthAudio';
import { WasmHearthEngine } from './host/wasmEngine';

export default function App() {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const engineRef = useRef<WasmHearthEngine | null>(null);
  const atlasRef = useRef<PixelAtlas | null>(null);
  const cutsceneOpsRef = useRef<CutsceneOpExport[]>([]);
  const rendererRef = useRef<SoftwarePpuRenderer | null>(null);
  const inputRef = useRef<RetroInputAdapter | null>(null);
  const audioRef = useRef<RetroSynthAudio | null>(null);

  if (!rendererRef.current) rendererRef.current = new SoftwarePpuRenderer();
  if (!inputRef.current) inputRef.current = new RetroInputAdapter();
  if (!audioRef.current) audioRef.current = new RetroSynthAudio();

  const [ui, setUi] = useState<UiSnapshot | null>(null);
  const [loadError, setLoadError] = useState<string | null>(null);
  const [workbenchOpen, setWorkbenchOpen] = useState(false);
  const [controlsOpen, setControlsOpen] = useState(false);
  const [crtEnabled, setCrtEnabled] = useState(true);
  const [muted, setMuted] = useState(false);

  const unlockAudio = useCallback(() => {
    audioRef.current?.unlock();
  }, []);

  const syncAfterUiAction = useCallback(() => {
    const engine = engineRef.current;
    const audio = audioRef.current;
    if (!engine) return;
    if (audio) {
      const pendingSfx = engine.consumePendingSfx();
      if (pendingSfx.length > 0) {
        audio.processQueue(pendingSfx);
      }
    }
    setUi(engine.getUiSnapshot());
  }, []);

  /* Load WebAssembly C99 Core & Run Fixed-Timestep 60Hz Engine Loop */
  useEffect(() => {
    const renderer = rendererRef.current!;
    const input = inputRef.current!;
    const audio = audioRef.current!;

    input.attach();

    let cancelled = false;
    let rafId = 0;

    const onGlobalKey = (e: KeyboardEvent) => {
      if (e.code === 'Tab') {
        e.preventDefault();
        setWorkbenchOpen((prev) => !prev);
      }
      audio.unlock();
    };
    window.addEventListener('keydown', onGlobalKey);

    WasmHearthEngine.create('/hearth_core.wasm')
      .then((engine) => {
        if (cancelled) return;
        engineRef.current = engine;
        atlasRef.current = new PixelAtlas(engine.palettes);
        cutsceneOpsRef.current = engine.getCutsceneIntroOps();
        setUi(engine.getUiSnapshot());

        const atlas = atlasRef.current;
        let lastTime = performance.now();
        let accumulator = 0;
        const STEP_MS = 1000 / 60;
        let frameSinceSync = 0;

        const tick = (now: number) => {
          const elapsed = Math.min(64, now - lastTime);
          lastTime = now;
          accumulator += elapsed;

          let latestPacket = null;
          let steps = 0;
          while (accumulator >= STEP_MS && steps < 4) {
            const padBits = input.poll();
            latestPacket = engine.step(padBits);
            const isBossBattle = latestPacket.battle?.enemies.some((e) => e.isBoss) ?? false;
            audio.setMusicMode(latestPacket.mode, isBossBattle);
            if (latestPacket.sfxQueue.length > 0) {
              audio.processQueue(latestPacket.sfxQueue);
            }
            accumulator -= STEP_MS;
            steps++;
          }

          if (latestPacket && canvasRef.current) {
            renderer.render(
              canvasRef.current,
              latestPacket,
              engine.map,
              atlas,
              engine.getUiSnapshot().party
            );
          }

          frameSinceSync += steps;
          if (frameSinceSync >= 3) {
            frameSinceSync = 0;
            setUi(engine.getUiSnapshot());
          }

          rafId = window.requestAnimationFrame(tick);
        };

        rafId = window.requestAnimationFrame(tick);
      })
      .catch((err) => {
        if (!cancelled) {
          setLoadError(err instanceof Error ? err.message : 'Failed to load WebAssembly core');
        }
      });

    return () => {
      cancelled = true;
      window.cancelAnimationFrame(rafId);
      window.removeEventListener('keydown', onGlobalKey);
      input.detach();
      audio.dispose();
    };
  }, []);

  const blurFocus = () => {
    (document.activeElement as HTMLElement | null)?.blur?.();
  };

  const handleStartSandbox = () => {
    if (!engineRef.current) return;
    blurFocus();
    unlockAudio();
    engineRef.current.startSandbox();
    syncAfterUiAction();
  };

  const handleStartIntro = () => {
    if (!engineRef.current) return;
    blurFocus();
    unlockAudio();
    engineRef.current.startIntro();
    syncAfterUiAction();
  };

  const handleSkipIntro = () => {
    if (!engineRef.current) return;
    blurFocus();
    unlockAudio();
    engineRef.current.skipIntro();
    syncAfterUiAction();
  };

  const handleJumpBattle = (preset: EncounterPreset = 'SHRINE_BOSS') => {
    if (!engineRef.current) return;
    blurFocus();
    unlockAudio();
    engineRef.current.startQuickBattle(preset);
    syncAfterUiAction();
  };

  const handleToggleMute = () => {
    unlockAudio();
    const next = !muted;
    setMuted(next);
    audioRef.current!.setMuted(next);
  };

  const handleUpdateTuning = (partial: Partial<EngineTuning>) => {
    if (!engineRef.current) return;
    engineRef.current.setTuning(partial);
    syncAfterUiAction();
  };

  const handleCanvasClick = () => {
    if (!engineRef.current) return;
    unlockAudio();
    engineRef.current.uiTriggerTimedTap();
    syncAfterUiAction();
  };

  const handleBattleCommand = (cmdIdx: number) => {
    if (!engineRef.current) return;
    unlockAudio();
    engineRef.current.uiBattleCommand(cmdIdx);
    syncAfterUiAction();
  };

  const handleSwapFamiliar = (slot: FamiliarSlot) => {
    if (!engineRef.current) return;
    unlockAudio();
    engineRef.current.uiBattleSwapFamiliar(slot);
    syncAfterUiAction();
  };

  const handleSelectEnemy = (idx: number) => {
    if (!engineRef.current) return;
    unlockAudio();
    engineRef.current.uiBattleSelectEnemy(idx);
    syncAfterUiAction();
  };

  const handleSelectChoice = (idx: number) => {
    if (!engineRef.current) return;
    unlockAudio();
    engineRef.current.uiSelectDialogueChoice(idx);
    syncAfterUiAction();
  };

  const isSnes = ui?.tuning.ppuMode === 'SNES_256x224';
  const activeMember = ui ? ui.party[ui.activeSlot] : null;

  return (
    <div className="flex h-screen w-screen flex-col overflow-hidden bg-[#11131A] text-[#ECE7DC]">
      {/* Strict 3-Zone Top Bar Contract */}
      <header className="flex h-13 shrink-0 items-center justify-between gap-8 border-b border-[#232838] bg-[#141721] px-6">
        {/* Zone 1: Clean Single-Line Wordmark */}
        <button
          type="button"
          onClick={() => {
            if (!engineRef.current) return;
            engineRef.current.returnToTitle();
            syncAfterUiAction();
          }}
          className="font-display text-lg font-semibold tracking-tight text-[#F6F1E6] whitespace-nowrap shrink-0 hover:text-[#F5B841] transition-colors"
        >
          Project Hearth
        </button>

        {/* Zone 2: 4–5 Clean Single-Line Nav Actions */}
        <nav className="hidden md:flex items-center gap-6 text-xs font-medium text-[#A4ADBF]">
          <button
            type="button"
            onClick={handleStartSandbox}
            className="whitespace-nowrap shrink-0 transition-colors hover:text-[#F6F1E6]"
          >
            Sandbox
          </button>
          <button
            type="button"
            onClick={() => handleJumpBattle('SHRINE_BOSS')}
            className="whitespace-nowrap shrink-0 transition-colors hover:text-[#F6F1E6]"
          >
            Battle Arena
          </button>
          <button
            type="button"
            onClick={handleStartIntro}
            className="whitespace-nowrap shrink-0 transition-colors hover:text-[#F6F1E6]"
          >
            Opening Intro
          </button>
          <button
            type="button"
            onClick={() => setControlsOpen(true)}
            className="whitespace-nowrap shrink-0 transition-colors hover:text-[#F6F1E6]"
          >
            Controls
          </button>
          <button
            type="button"
            onClick={handleToggleMute}
            className="flex items-center gap-1.5 whitespace-nowrap shrink-0 transition-colors hover:text-[#F6F1E6]"
          >
            {muted ? <VolumeX className="h-3.5 w-3.5" /> : <Volume2 className="h-3.5 w-3.5" />}
            {muted ? 'Muted' : 'Audio On'}
          </button>
        </nav>

        {/* Zone 3: Single Primary Action */}
        <div className="flex items-center shrink-0">
          <button
            type="button"
            onClick={() => setWorkbenchOpen((v) => !v)}
            className={`flex items-center gap-2 rounded-md border px-3.5 py-1.5 text-xs font-semibold whitespace-nowrap shrink-0 transition-colors ${
              workbenchOpen
                ? 'border-[#D4942A] bg-[#D4942A] text-[#141721]'
                : 'border-[#31384D] bg-[#1C2130] text-[#F6F1E6] hover:border-[#D4942A]'
            }`}
          >
            <Sliders className="h-3.5 w-3.5" />
            Engine Workbench
          </button>
        </div>
      </header>

      {/* Main Theater + Optional Collapsible Workbench Drawer */}
      <div className="flex min-h-0 flex-1 overflow-hidden">
        <main className="flex min-w-0 flex-1 flex-col items-center justify-between overflow-hidden bg-[#0E1017]">
          {/* Centered 16-Bit Virtual PPU Monitor */}
          <div className="relative flex min-h-0 w-full flex-1 items-center justify-center p-3 sm:p-5">
            <div
              className={`relative flex max-h-full max-w-full items-center justify-center overflow-hidden rounded-lg border-2 border-[#2A3042] bg-[#0A0C12] shadow-2xl ${
                isSnes ? 'aspect-[256/224]' : 'aspect-[320/224]'
              }`}
              style={{ height: '100%', width: 'auto' }}
            >
              {/* Nearest-Neighbor Virtual PPU Canvas */}
              <canvas
                ref={canvasRef}
                width={isSnes ? 256 : 320}
                height={224}
                onClick={handleCanvasClick}
                className="pixelated h-full w-full cursor-pointer select-none object-contain"
              />

              {/* Subtle Optional CRT Scanline Phosphor Mask */}
              {crtEnabled && (
                <div className="crt-scanlines pointer-events-none absolute inset-0 opacity-65" />
              )}

              {/* Loading / Error State */}
              {!ui && (
                <div className="absolute inset-0 flex items-center justify-center bg-[#0E1017]/90 px-6 text-center font-mono text-xs text-[#AEB6C8]">
                  {loadError ? `Error: ${loadError}` : 'Initializing C99 WebAssembly Core...'}
                </div>
              )}

              {/* INTRO MODE QUICK SKIP BUTTON (Keyboard Space/Enter/Esc also works) */}
              {ui?.mode === 'INTRO' && (
                <button
                  type="button"
                  onClick={(e) => {
                    e.stopPropagation();
                    handleSkipIntro();
                  }}
                  className="rpg-slot font-pixel absolute top-3 right-3 z-20 px-2.5 py-1 text-[10px] tracking-wide text-[#F5D688] transition-colors hover:border-[#E7AA40] hover:text-[#FFF6DC]"
                >
                  SKIP &gt;&gt;
                </button>
              )}

              {/* TITLE SCREEN OVERLAY */}
              {ui?.mode === 'TITLE' && (
                <div className="absolute inset-0 flex flex-col items-center justify-center bg-gradient-to-t from-[#0E1018]/88 via-[#0E1018]/52 to-[#0E1018]/35 px-5 text-center">
                  <div className="rpg-window max-w-md px-6 py-5">
                    <p className="font-pixel text-[10px] tracking-widest text-[#E5B85B]">
                      CHRONICLE OF WINDMILL HAMLET
                    </p>
                    <h1
                      className="mt-2.5 font-pixel-title text-xl leading-snug tracking-tight text-[#FFF6DC] sm:text-2xl"
                      style={{ textWrap: 'balance' }}
                    >
                      PROJECT HEARTH
                    </h1>
                    <p className="mt-2.5 font-pixel text-[11px] leading-relaxed text-[#CFC8B8]">
                      16-BIT STORYBOOK RPG · FAMILIAR SWAPPING &amp; TIMED COMBAT
                    </p>

                    <div className="mt-5 flex flex-wrap items-center justify-center gap-2.5">
                      <button
                        type="button"
                        onClick={handleStartSandbox}
                        className="font-pixel flex items-center gap-2 border-2 border-[#F5D07A] bg-[#C98E32] px-4 py-2 text-xs font-bold text-[#14121A] whitespace-nowrap transition-colors hover:bg-[#E2A84A]"
                      >
                        <Play className="h-3.5 w-3.5 fill-current" />
                        START ADVENTURE
                      </button>
                      <button
                        type="button"
                        onClick={() => handleJumpBattle('SHRINE_BOSS')}
                        className="rpg-slot font-pixel flex items-center gap-1.5 px-3.5 py-2 text-xs text-[#F2E9D4] whitespace-nowrap transition-colors hover:border-[#D4A454]"
                      >
                        <Swords className="h-3.5 w-3.5 text-[#F5B841]" />
                        BOSS ARENA
                      </button>
                      <button
                        type="button"
                        onClick={handleStartIntro}
                        className="rpg-slot font-pixel flex items-center gap-1.5 px-3.5 py-2 text-xs text-[#F2E9D4] whitespace-nowrap transition-colors hover:border-[#6CC8E8]"
                      >
                        <Film className="h-3.5 w-3.5 text-[#6CC8E8]" />
                        REPLAY INTRO
                      </button>
                    </div>

                    <div className="mt-4 flex flex-wrap items-center justify-center gap-x-3 gap-y-1 font-pixel text-[10px] text-[#ABA494]">
                      <span>WASD/ARROWS: MOVE</span>
                      <span>SHIFT: DASH</span>
                      <span>SPACE: ACT/TIMED HIT</span>
                      <span>Q/E: SWAP FAMILIAR</span>
                    </div>
                  </div>
                </div>
              )}

              {/* COHESIVE 16-BIT STORYBOOK DIALOGUE OVERLAY */}
              {ui && ui.dialogue.active && (ui.mode === 'EXPLORE' || ui.mode === 'CUTSCENE') && (
                <div
                  className="rpg-window absolute inset-x-3 bottom-2.5 z-20 px-3.5 py-3 sm:inset-x-4 sm:bottom-3.5"
                  onClick={handleCanvasClick}
                >
                  <div className="flex items-start gap-3">
                    <PortraitFrame
                      key={ui.dialogue.portrait}
                      portrait={ui.dialogue.portrait}
                      speaker={ui.dialogue.speaker}
                    />
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center justify-between gap-2 border-b border-[#3F3528] pb-1">
                        <span className="font-pixel text-xs font-bold tracking-wide text-[#F3C86A]">
                          {ui.dialogue.speaker}
                        </span>
                        <span className="font-pixel text-[9px] text-[#9E9584]">
                          {ui.dialogue.finishedTyping ? '[SPACE / CLICK]' : '...'}
                        </span>
                      </div>
                      <p className="mt-1.5 font-pixel text-xs leading-relaxed text-[#F2EBDC] sm:text-[13px]">
                        {ui.dialogue.fullText.slice(0, ui.dialogue.visibleChars)}
                      </p>

                      {ui.dialogue.finishedTyping && ui.dialogue.choices.length > 0 && (
                        <div className="mt-2.5 flex flex-col gap-1.5">
                          {ui.dialogue.choices.map((ch, idx) => (
                            <button
                              key={`${ch.actionId}_${idx}`}
                              type="button"
                              onClick={(e) => {
                                e.stopPropagation();
                                handleSelectChoice(idx);
                              }}
                              className={`font-pixel flex items-center gap-1.5 px-2.5 py-1.5 text-left text-[11px] transition-colors ${
                                ui.dialogue.selectedChoice === idx
                                  ? 'rpg-slot-active text-[#FFF4CF]'
                                  : 'rpg-slot text-[#CEC6B5] hover:border-[#B89152]'
                              }`}
                            >
                              <ChevronRight className="h-3 w-3 shrink-0 text-[#F3C86A]" />
                              <span>{ch.label}</span>
                            </button>
                          ))}
                        </div>
                      )}
                    </div>
                  </div>
                </div>
              )}

              {/* BATTLE VICTORY / DEFEAT SUMMARY MODAL */}
              {ui && (ui.mode === 'BATTLE_VICTORY' || ui.mode === 'BATTLE_DEFEAT') && (
                <div className="absolute inset-0 z-30 flex items-center justify-center bg-[#0C0E16]/78 p-4">
                  <div className="rpg-window w-full max-w-sm p-5 text-center">
                    <h2 className="font-pixel-title text-base text-[#FFF5D4]">
                      {ui.mode === 'BATTLE_VICTORY' ? 'VICTORY!' : 'RECOVERED'}
                    </h2>
                    <p className="mt-2 font-pixel text-[11px] text-[#B8B0A0]">
                      {ui.mode === 'BATTLE_VICTORY'
                        ? 'SMOOTH FAMILIAR WORK!'
                        : 'RESTORED AT THE HAMLET WAYSTONE.'}
                    </p>

                    {ui.mode === 'BATTLE_VICTORY' && (
                      <div className="rpg-slot mt-3.5 grid grid-cols-3 gap-2 p-2.5 font-pixel text-[10px] tabular-nums">
                        <div>
                          <div className="text-[#9E9584]">TIMED HITS</div>
                          <div className="mt-1 text-sm font-bold text-[#F5C45A]">
                            {ui.battleStats.combos}
                          </div>
                        </div>
                        <div>
                          <div className="text-[#9E9584]">PARRIES</div>
                          <div className="mt-1 text-sm font-bold text-[#6CC8E8]">
                            {ui.battleStats.parries}
                          </div>
                        </div>
                        <div>
                          <div className="text-[#9E9584]">LOCKS</div>
                          <div className="mt-1 text-sm font-bold text-[#7BC472]">
                            {ui.battleStats.locksBroken}
                          </div>
                        </div>
                      </div>
                    )}

                    <div className="mt-4 flex items-center justify-center gap-2.5">
                      <button
                        type="button"
                        onClick={() => {
                          engineRef.current?.uiFinishBattleSummary(false);
                          syncAfterUiAction();
                        }}
                        className="font-pixel border-2 border-[#F5D07A] bg-[#C98E32] px-3.5 py-1.5 text-xs font-bold text-[#14121A] whitespace-nowrap hover:bg-[#E2A84A]"
                      >
                        CONTINUE
                      </button>
                      <button
                        type="button"
                        onClick={() => {
                          engineRef.current?.uiFinishBattleSummary(true);
                          syncAfterUiAction();
                        }}
                        className="rpg-slot font-pixel flex items-center gap-1.5 px-3 py-1.5 text-xs text-[#ECE4D2] whitespace-nowrap hover:border-[#D4A454]"
                      >
                        <RotateCcw className="h-3 w-3" />
                        REMATCH
                      </button>
                    </div>
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* BOTTOM CONTEXT BAR: Seamless swap between Battle HUD and Clean Exploration Strip */}
          {ui?.mode === 'BATTLE' ? (
            <BattleHudOverlay
              party={ui.party}
              activeSlot={ui.activeSlot}
              potions={ui.potions}
              phase={ui.battlePhase}
              selectedCommand={ui.battleSelectedCommand}
              timedRingActive={ui.timedRingActive}
              timedRingKind={ui.timedRingKind}
              enemies={ui.battleEnemies}
              selectedEnemy={ui.battleSelectedEnemy}
              onCommand={handleBattleCommand}
              onSwapFamiliar={handleSwapFamiliar}
              onSelectEnemy={handleSelectEnemy}
              onTimedTap={handleCanvasClick}
            />
          ) : (
            <footer className="flex w-full shrink-0 flex-wrap items-center justify-between gap-4 border-t border-[#232838] bg-[#141721] px-6 py-2.5 text-xs text-[#AEB6C8]">
              <div className="flex flex-wrap items-center gap-3 min-w-0">
                <span className="flex items-center gap-1.5 font-medium text-[#F6F1E6] whitespace-nowrap">
                  <Compass className="h-3.5 w-3.5 text-[#F5B841]" />
                  {ui?.zoneLabel ?? 'Windmill Hamlet'}
                </span>
                <span aria-hidden="true" className="text-[#4B546B]">
                  ·
                </span>
                <span className="truncate text-[#C7CFDF]">
                  {ui?.objectiveLabel ?? 'Loading C99 WebAssembly engine...'}
                </span>
              </div>

              {ui && activeMember && (
                <div className="flex flex-wrap items-center gap-4 font-mono text-[11px] tabular-nums">
                  <span className="flex items-center gap-1.5 whitespace-nowrap">
                    <Heart
                      className={`h-3.5 w-3.5 ${
                        ui.locket.hasCourage
                          ? 'text-[#F5B841] fill-[#F5B841]'
                          : ui.locket.guardMended
                            ? 'text-[#74B461] fill-[#74B461]'
                            : 'text-[#6A738A]'
                      }`}
                    />
                    {ui.locket.hasCourage
                      ? 'Locket: Courage'
                      : ui.locket.guardMended
                        ? 'Bram Mended'
                        : 'Locket: Empty'}
                  </span>
                  <span aria-hidden="true" className="text-[#4B546B]">
                    /
                  </span>
                  <span className="text-[#E6E0D4] whitespace-nowrap">
                    Lead: {activeMember.name} ({activeMember.hp}/{activeMember.maxHp} HP)
                  </span>
                  <span aria-hidden="true" className="text-[#4B546B]">
                    /
                  </span>
                  <span className="text-[#8E98B0] whitespace-nowrap">
                    {isSnes ? '256x224 SNES' : '320x224 Genesis'} · Tab: Workbench
                  </span>
                </div>
              )}
            </footer>
          )}
        </main>

        {/* Optional Collapsible Engine Workbench (Feel Tuner, PNG Swapper, C99 Exporter) */}
        {ui && atlasRef.current && engineRef.current && (
          <EngineWorkbench
            open={workbenchOpen}
            onClose={() => setWorkbenchOpen(false)}
            tuning={ui.tuning}
            onUpdateTuning={handleUpdateTuning}
            crtEnabled={crtEnabled}
            onToggleCrt={() => setCrtEnabled((v) => !v)}
            oamCount={ui.oamCount}
            oamLimit={ui.oamLimit}
            playerTileX={ui.playerTileX}
            playerTileY={ui.playerTileY}
            activeEnemyCount={ui.activeEnemyCount}
            onRespawnEnemies={() => {
              engineRef.current?.respawnFieldEnemies();
              syncAfterUiAction();
            }}
            onHealParty={() => {
              unlockAudio();
              engineRef.current?.healPartyFull();
              syncAfterUiAction();
            }}
            onJumpBattle={(preset) => handleJumpBattle(preset)}
            atlas={atlasRef.current}
            mapData={engineRef.current.map}
            palettes={engineRef.current.palettes}
            cutsceneOps={cutsceneOpsRef.current}
          />
        )}
      </div>

      <ControlsModal open={controlsOpen} onClose={() => setControlsOpen(false)} />
    </div>
  );
}
