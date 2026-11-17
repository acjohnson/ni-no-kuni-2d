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
import { EncounterPreset } from './core/battleSystem';
import { HearthEngine } from './core/engine';
import { EngineTuning, FamiliarSlot, UiSnapshot } from './core/types';
import { RetroInputAdapter } from './host/inputAdapter';
import { PixelAtlas } from './host/pixelAtlas';
import { SoftwarePpuRenderer } from './host/ppuRenderer';
import { RetroSynthAudio } from './host/synthAudio';

export default function App() {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);
  const engineRef = useRef<HearthEngine | null>(null);
  const atlasRef = useRef<PixelAtlas | null>(null);
  const rendererRef = useRef<SoftwarePpuRenderer | null>(null);
  const inputRef = useRef<RetroInputAdapter | null>(null);
  const audioRef = useRef<RetroSynthAudio | null>(null);

  if (!engineRef.current) engineRef.current = new HearthEngine();
  if (!atlasRef.current) atlasRef.current = new PixelAtlas();
  if (!rendererRef.current) rendererRef.current = new SoftwarePpuRenderer();
  if (!inputRef.current) inputRef.current = new RetroInputAdapter();
  if (!audioRef.current) audioRef.current = new RetroSynthAudio();

  const [ui, setUi] = useState<UiSnapshot>(() => engineRef.current!.getUiSnapshot());
  const [workbenchOpen, setWorkbenchOpen] = useState(false);
  const [controlsOpen, setControlsOpen] = useState(false);
  const [crtEnabled, setCrtEnabled] = useState(true);
  const [muted, setMuted] = useState(false);

  const unlockAudio = useCallback(() => {
    audioRef.current?.unlock();
  }, []);

  /* Fixed-Timestep 60Hz Engine Loop Decoupled from React State */
  useEffect(() => {
    const engine = engineRef.current!;
    const atlas = atlasRef.current!;
    const renderer = rendererRef.current!;
    const input = inputRef.current!;
    const audio = audioRef.current!;

    input.attach();

    let rafId = 0;
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
        audio.setMusicMode(latestPacket.mode);
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

      // Sync lightweight React HUD snapshot ~15Hz (every 4 ticks) for smooth HUD & zero jitter
      frameSinceSync += steps;
      if (frameSinceSync >= 3) {
        frameSinceSync = 0;
        setUi(engine.getUiSnapshot());
      }

      rafId = window.requestAnimationFrame(tick);
    };

    rafId = window.requestAnimationFrame(tick);

    const onGlobalKey = (e: KeyboardEvent) => {
      if (e.code === 'Tab') {
        e.preventDefault();
        setWorkbenchOpen((prev) => !prev);
      }
      audio.unlock();
    };
    window.addEventListener('keydown', onGlobalKey);

    return () => {
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
    blurFocus();
    unlockAudio();
    engineRef.current!.startSandbox();
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleStartCutscene = () => {
    blurFocus();
    unlockAudio();
    engineRef.current!.startCutsceneDemo(false);
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleJumpBattle = (preset: EncounterPreset = 'SHRINE_BOSS') => {
    blurFocus();
    unlockAudio();
    engineRef.current!.startQuickBattle(preset);
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleToggleMute = () => {
    unlockAudio();
    const next = !muted;
    setMuted(next);
    audioRef.current!.setMuted(next);
  };

  const handleUpdateTuning = (partial: Partial<EngineTuning>) => {
    engineRef.current!.setTuning(partial);
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleCanvasClick = () => {
    unlockAudio();
    engineRef.current!.uiTriggerTimedTap();
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleBattleCommand = (cmdIdx: number) => {
    unlockAudio();
    engineRef.current!.uiBattleCommand(cmdIdx);
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleSwapFamiliar = (slot: FamiliarSlot) => {
    unlockAudio();
    engineRef.current!.uiBattleSwapFamiliar(slot);
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleSelectEnemy = (idx: number) => {
    unlockAudio();
    engineRef.current!.uiBattleSelectEnemy(idx);
    setUi(engineRef.current!.getUiSnapshot());
  };

  const handleSelectChoice = (idx: number) => {
    unlockAudio();
    engineRef.current!.uiSelectDialogueChoice(idx);
    setUi(engineRef.current!.getUiSnapshot());
  };

  const isSnes = ui.tuning.ppuMode === 'SNES_256x224';
  const activeMember = ui.party[ui.activeSlot];

  return (
    <div className="flex h-screen w-screen flex-col overflow-hidden bg-[#11131A] text-[#ECE7DC]">
      {/* Strict 3-Zone Top Bar Contract */}
      <header className="flex h-13 shrink-0 items-center justify-between gap-8 border-b border-[#232838] bg-[#141721] px-6">
        {/* Zone 1: Clean Single-Line Wordmark */}
        <button
          type="button"
          onClick={() => {
            engineRef.current!.returnToTitle();
            setUi(engineRef.current!.getUiSnapshot());
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
            onClick={handleStartCutscene}
            className="whitespace-nowrap shrink-0 transition-colors hover:text-[#F6F1E6]"
          >
            Cutscene Demo
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

              {/* TITLE SCREEN OVERLAY */}
              {ui.mode === 'TITLE' && (
                <div className="absolute inset-0 flex flex-col items-center justify-center bg-gradient-to-t from-[#11141E]/92 via-[#11141E]/62 to-[#11141E]/45 px-6 text-center">
                  <div className="max-w-lg">
                    <p className="font-mono text-xs tracking-wider text-[#F5B841]">
                      16-BIT CUSTOM ENGINE VERTICAL SLICE · C99-PORTABLE CORE
                    </p>
                    <h1
                      className="mt-2 font-display text-4xl font-bold tracking-tight text-[#FBF7EE] sm:text-5xl"
                      style={{ textWrap: 'balance' }}
                    >
                      Project Hearth
                    </h1>
                    <p className="mt-3 font-serif text-sm leading-relaxed text-[#D8D3C8] sm:text-base">
                      Tactile 8-way sub-pixel exploration, fluid familiar hot-swapping, timed hits
                      and parries, and bytecode cutscenes—built cleanly for retro hardware.
                    </p>

                    <div className="mt-6 flex flex-wrap items-center justify-center gap-3">
                      <button
                        type="button"
                        onClick={handleStartSandbox}
                        className="flex items-center gap-2 rounded-md bg-[#D4942A] px-5 py-2.5 text-sm font-semibold text-[#141721] whitespace-nowrap transition-colors hover:bg-[#E7AA40]"
                      >
                        <Play className="h-4 w-4 fill-current" />
                        Start Sandbox
                      </button>
                      <button
                        type="button"
                        onClick={() => handleJumpBattle('SHRINE_BOSS')}
                        className="flex items-center gap-2 rounded-md border border-[#404963] bg-[#1A1F2E]/90 px-4 py-2.5 text-sm font-medium text-[#F4EFE4] whitespace-nowrap transition-colors hover:border-[#D4942A]"
                      >
                        <Swords className="h-4 w-4 text-[#F5B841]" />
                        Jump to Battle Arena
                      </button>
                      <button
                        type="button"
                        onClick={handleStartCutscene}
                        className="flex items-center gap-2 rounded-md border border-[#404963] bg-[#1A1F2E]/90 px-4 py-2.5 text-sm font-medium text-[#F4EFE4] whitespace-nowrap transition-colors hover:border-[#5AD2F4]"
                      >
                        <Film className="h-4 w-4 text-[#5AD2F4]" />
                        Cutscene Test
                      </button>
                    </div>

                    <div className="mt-5 flex flex-wrap items-center justify-center gap-3 font-mono text-[11px] text-[#AEB6C8]">
                      <span>WASD / Arrows: Move</span>
                      <span aria-hidden="true">·</span>
                      <span>Shift: Dash</span>
                      <span aria-hidden="true">·</span>
                      <span>Space / Click: Interact &amp; Timed Hits</span>
                      <span aria-hidden="true">·</span>
                      <span>Q / E: Swap Familiar</span>
                    </div>
                  </div>
                </div>
              )}

              {/* PAINTED PORTRAIT DIALOGUE & CUTSCENE OVERLAY */}
              {ui.dialogue.active && (ui.mode === 'EXPLORE' || ui.mode === 'CUTSCENE') && (
                <div
                  className="absolute inset-x-3 bottom-3 z-20 rounded-md border-2 border-[#D4942A]/80 bg-[#171C2B]/96 p-3.5 shadow-2xl sm:inset-x-5 sm:bottom-4 sm:p-4"
                  onClick={handleCanvasClick}
                >
                  <div className="flex items-start gap-3.5 sm:gap-4">
                    <PortraitFrame
                      key={ui.dialogue.portrait}
                      portrait={ui.dialogue.portrait}
                      speaker={ui.dialogue.speaker}
                    />
                    <div className="min-w-0 flex-1">
                      <div className="flex items-center justify-between gap-2">
                        <span className="font-display text-sm font-semibold text-[#F5B841]">
                          {ui.dialogue.speaker}
                        </span>
                        <span className="font-mono text-[10px] text-[#8D97AE]">
                          {ui.dialogue.finishedTyping ? 'Space / Click' : 'Typing...'}
                        </span>
                      </div>
                      <p className="mt-1.5 font-serif text-sm leading-relaxed text-[#F4EFE4] sm:text-[15px]">
                        {ui.dialogue.fullText.slice(0, ui.dialogue.visibleChars)}
                      </p>

                      {ui.dialogue.finishedTyping && ui.dialogue.choices.length > 0 && (
                        <div className="mt-3 flex flex-wrap gap-2">
                          {ui.dialogue.choices.map((ch, idx) => (
                            <button
                              key={ch.actionId}
                              type="button"
                              onClick={(e) => {
                                e.stopPropagation();
                                handleSelectChoice(idx);
                              }}
                              className={`flex items-center gap-1.5 rounded border px-3 py-1.5 text-xs font-medium whitespace-nowrap transition-colors ${
                                ui.dialogue.selectedChoice === idx
                                  ? 'border-[#F5B841] bg-[#D4942A]/20 text-[#FFF3CE]'
                                  : 'border-[#333C54] bg-[#1F2538] text-[#D7DCE8] hover:border-[#D4942A]'
                              }`}
                            >
                              <ChevronRight className="h-3.5 w-3.5 text-[#F5B841]" />
                              {ch.label}
                            </button>
                          ))}
                        </div>
                      )}
                    </div>
                  </div>
                </div>
              )}

              {/* BATTLE VICTORY / DEFEAT SUMMARY MODAL */}
              {(ui.mode === 'BATTLE_VICTORY' || ui.mode === 'BATTLE_DEFEAT') && (
                <div className="absolute inset-0 z-30 flex items-center justify-center bg-[#10131C]/82 p-4">
                  <div className="w-full max-w-sm rounded-lg border-2 border-[#D4942A]/80 bg-[#171C2B] p-5 text-center shadow-2xl">
                    <h2 className="font-display text-2xl font-bold text-[#FBF7EE]">
                      {ui.mode === 'BATTLE_VICTORY' ? 'Victory!' : 'Recovered at Waystone'}
                    </h2>
                    <p className="mt-1 text-xs text-[#9AA3B8]">
                      {ui.mode === 'BATTLE_VICTORY'
                        ? 'Smooth familiar work! Review your combat feel stats below.'
                        : 'Your party was restored to full HP & MP at the Hamlet Waystone.'}
                    </p>

                    {ui.mode === 'BATTLE_VICTORY' && (
                      <div className="mt-4 grid grid-cols-3 gap-2 rounded border border-[#283044] bg-[#111520] p-3 font-mono text-xs tabular-nums">
                        <div>
                          <div className="text-[10px] text-[#8B95AC]">Timed Hits</div>
                          <div className="mt-0.5 text-base font-semibold text-[#F5B841]">
                            {ui.battleStats.combos}
                          </div>
                        </div>
                        <div>
                          <div className="text-[10px] text-[#8B95AC]">Parries</div>
                          <div className="mt-0.5 text-base font-semibold text-[#5AD2F4]">
                            {ui.battleStats.parries}
                          </div>
                        </div>
                        <div>
                          <div className="text-[10px] text-[#8B95AC]">Locks Broken</div>
                          <div className="mt-0.5 text-base font-semibold text-[#74B461]">
                            {ui.battleStats.locksBroken}
                          </div>
                        </div>
                      </div>
                    )}

                    <div className="mt-5 flex items-center justify-center gap-2.5">
                      <button
                        type="button"
                        onClick={() => {
                          engineRef.current!.uiFinishBattleSummary(false);
                          setUi(engineRef.current!.getUiSnapshot());
                        }}
                        className="rounded-md bg-[#D4942A] px-4 py-2 text-xs font-semibold text-[#141721] whitespace-nowrap hover:bg-[#E7AA40]"
                      >
                        Return to Field
                      </button>
                      <button
                        type="button"
                        onClick={() => {
                          engineRef.current!.uiFinishBattleSummary(true);
                          setUi(engineRef.current!.getUiSnapshot());
                        }}
                        className="flex items-center gap-1.5 rounded-md border border-[#3B445E] bg-[#1E2436] px-3.5 py-2 text-xs font-medium text-[#ECE7DC] whitespace-nowrap hover:border-[#D4942A]"
                      >
                        <RotateCcw className="h-3.5 w-3.5" />
                        Rematch
                      </button>
                    </div>
                  </div>
                </div>
              )}
            </div>
          </div>

          {/* BOTTOM CONTEXT BAR: Seamless swap between Battle HUD and Clean Exploration Strip */}
          {ui.mode === 'BATTLE' ? (
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
                  {ui.zoneLabel}
                </span>
                <span aria-hidden="true" className="text-[#4B546B]">
                  ·
                </span>
                <span className="truncate text-[#C7CFDF]">{ui.objectiveLabel}</span>
              </div>

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
            </footer>
          )}
        </main>

        {/* Optional Collapsible Engine Workbench (Feel Tuner, PNG Swapper, C99 Exporter) */}
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
            engineRef.current!.respawnFieldEnemies();
            setUi(engineRef.current!.getUiSnapshot());
          }}
          onHealParty={() => {
            unlockAudio();
            engineRef.current!.healPartyFull();
            setUi(engineRef.current!.getUiSnapshot());
          }}
          onJumpBattle={(preset) => handleJumpBattle(preset)}
          atlas={atlasRef.current!}
          mapData={engineRef.current!.map}
        />
      </div>

      <ControlsModal open={controlsOpen} onClose={() => setControlsOpen(false)} />
    </div>
  );
}
