import React, { useMemo, useState } from 'react';
import {
  Check,
  Code2,
  Copy,
  Download,
  Image as ImageIcon,
  RotateCcw,
  Sliders,
  Swords,
  Upload,
  X,
} from 'lucide-react';
import { WorldMapData } from '../core/mapData';
import { DEFAULT_TUNING, EngineTuning, PpuMode } from '../core/types';
import {
  C99Target,
  generateHearthDataHeader,
  generateHearthTypesHeader,
} from '../host/c99Exporter';
import { PixelAtlas, SwappableSlotId } from '../host/pixelAtlas';

interface EngineWorkbenchProps {
  open: boolean;
  onClose: () => void;
  tuning: EngineTuning;
  onUpdateTuning: (partial: Partial<EngineTuning>) => void;
  crtEnabled: boolean;
  onToggleCrt: () => void;
  oamCount: number;
  oamLimit: number;
  playerTileX: number;
  playerTileY: number;
  activeEnemyCount: number;
  onRespawnEnemies: () => void;
  onHealParty: () => void;
  onJumpBattle: (preset: 'MEADOW_PACK' | 'SHRINE_BOSS') => void;
  atlas: PixelAtlas;
  mapData: WorldMapData;
}

const SWAPPABLE_SLOTS: { id: SwappableSlotId; label: string; spec: string }[] = [
  { id: 'hero', label: 'Hero (Rowan)', spec: '16x22 px PNG' },
  { id: 'pip', label: 'Lantern Sprite (Pip)', spec: '16x18 px PNG' },
  { id: 'familiar_bramble', label: 'Familiar A (Bramblekit)', spec: '20x22 px PNG' },
  { id: 'familiar_lumina', label: 'Familiar B (Lumina)', spec: '20x22 px PNG' },
  { id: 'enemy_gloomcap', label: 'Enemy (Gloomcap)', spec: '20x20 px PNG' },
  { id: 'boss_briar_warden', label: 'Shrine Boss (Briar Warden)', spec: '32x34 px PNG' },
  { id: 'tileset_grass', label: 'Meadow Grass Tile', spec: '16x16 px PNG' },
];

export const EngineWorkbench: React.FC<EngineWorkbenchProps> = ({
  open,
  onClose,
  tuning,
  onUpdateTuning,
  crtEnabled,
  onToggleCrt,
  oamCount,
  oamLimit,
  playerTileX,
  playerTileY,
  activeEnemyCount,
  onRespawnEnemies,
  onHealParty,
  onJumpBattle,
  atlas,
  mapData,
}) => {
  const [tab, setTab] = useState<'feel' | 'assets' | 'c99'>('feel');
  const [c99Target, setC99Target] = useState<C99Target>('SGDK_GENESIS');
  const [c99File, setC99File] = useState<'types' | 'data'>('types');
  const [copied, setCopied] = useState(false);
  const [assetVersion, setAssetVersion] = useState(0);

  const generatedCode = useMemo(() => {
    return c99File === 'types'
      ? generateHearthTypesHeader(c99Target, tuning)
      : generateHearthDataHeader(c99Target, mapData);
  }, [c99Target, c99File, tuning, mapData]);

  if (!open) return null;

  const handleUploadAsset = (slot: SwappableSlotId, file: File | undefined) => {
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => {
      const img = new Image();
      img.onload = () => {
        atlas.setOverride(slot, img);
        setAssetVersion((v) => v + 1);
      };
      img.src = String(reader.result);
    };
    reader.readAsDataURL(file);
  };

  const handleResetAsset = (slot: SwappableSlotId) => {
    atlas.clearOverride(slot);
    setAssetVersion((v) => v + 1);
  };

  const handleCopyCode = async () => {
    try {
      await navigator.clipboard.writeText(generatedCode);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 1800);
    } catch {
      // Clipboard blocked in sandbox fallback: silently ignore
    }
  };

  const handleDownloadHeader = () => {
    const filename = c99File === 'types' ? 'hearth_types.h' : 'hearth_data.h';
    const blob = new Blob([generatedCode], { type: 'text/plain;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <aside className="flex h-full w-full max-w-[380px] shrink-0 flex-col border-l border-[#2B3142] bg-[#161923] text-[#ECE7DC]">
      {/* Drawer Header */}
      <div className="flex items-center justify-between border-b border-[#262C3C] px-4 py-3">
        <div>
          <h2 className="text-sm font-semibold tracking-tight text-[#F6F1E6]">
            Engine Workbench
          </h2>
          <p className="text-xs text-[#9AA3B8]">
            Live 60Hz C99-Mirror Registers &amp; Asset Hooks
          </p>
        </div>
        <button
          type="button"
          onClick={onClose}
          className="rounded-md p-1.5 text-[#9AA3B8] transition-colors hover:bg-[#232838] hover:text-white"
          aria-label="Close Engine Workbench"
        >
          <X className="h-4 w-4" />
        </button>
      </div>

      {/* Segmented Tab Controls */}
      <div className="flex gap-1 border-b border-[#262C3C] bg-[#12151E] p-1.5">
        <button
          type="button"
          onClick={() => setTab('feel')}
          className={`flex flex-1 items-center justify-center gap-1.5 rounded px-2.5 py-1.5 text-xs font-medium whitespace-nowrap transition-colors ${
            tab === 'feel'
              ? 'bg-[#262D40] text-[#F6F1E6]'
              : 'text-[#8F98AE] hover:text-[#D9DFEE]'
          }`}
        >
          <Sliders className="h-3.5 w-3.5" />
          Feel &amp; PPU
        </button>
        <button
          type="button"
          onClick={() => setTab('assets')}
          className={`flex flex-1 items-center justify-center gap-1.5 rounded px-2.5 py-1.5 text-xs font-medium whitespace-nowrap transition-colors ${
            tab === 'assets'
              ? 'bg-[#262D40] text-[#F6F1E6]'
              : 'text-[#8F98AE] hover:text-[#D9DFEE]'
          }`}
        >
          <ImageIcon className="h-3.5 w-3.5" />
          Assets
        </button>
        <button
          type="button"
          onClick={() => setTab('c99')}
          className={`flex flex-1 items-center justify-center gap-1.5 rounded px-2.5 py-1.5 text-xs font-medium whitespace-nowrap transition-colors ${
            tab === 'c99'
              ? 'bg-[#262D40] text-[#F6F1E6]'
              : 'text-[#8F98AE] hover:text-[#D9DFEE]'
          }`}
        >
          <Code2 className="h-3.5 w-3.5" />
          C99 Export
        </button>
      </div>

      {/* Tab Body */}
      <div className="flex-1 overflow-y-auto p-4">
        {tab === 'feel' && (
          <div className="space-y-5">
            {/* Hardware Budget Readout (Real Engine Metrics) */}
            <div className="grid grid-cols-3 gap-2 rounded-md border border-[#262C3C] bg-[#11141C] p-3 font-mono text-xs tabular-nums">
              <div>
                <div className="text-[11px] text-[#838DA3]">OAM Sprites</div>
                <div className="mt-0.5 font-semibold text-[#F5B841]">
                  {oamCount}/{oamLimit}
                </div>
              </div>
              <div>
                <div className="text-[11px] text-[#838DA3]">Tile Pos</div>
                <div className="mt-0.5 font-semibold text-[#ECE7DC]">
                  {playerTileX},{playerTileY}
                </div>
              </div>
              <div>
                <div className="text-[11px] text-[#838DA3]">Field Foes</div>
                <div className="mt-0.5 font-semibold text-[#74B461]">
                  {activeEnemyCount}/4
                </div>
              </div>
            </div>

            {/* Virtual PPU Mode Selector */}
            <div className="space-y-2">
              <div className="text-xs font-medium text-[#C8CFDE]">Virtual PPU Framebuffer</div>
              <div className="grid grid-cols-2 gap-1.5">
                {(
                  [
                    { id: 'GENESIS_320x224', label: 'Genesis 320x224' },
                    { id: 'SNES_256x224', label: 'SNES 256x224' },
                  ] as { id: PpuMode; label: string }[]
                ).map((opt) => (
                  <button
                    key={opt.id}
                    type="button"
                    onClick={() => onUpdateTuning({ ppuMode: opt.id })}
                    className={`rounded border px-2.5 py-2 text-xs font-medium whitespace-nowrap transition-colors ${
                      tuning.ppuMode === opt.id
                        ? 'border-[#D4942A] bg-[#D4942A]/15 text-[#FFF1C4]'
                        : 'border-[#2A3142] bg-[#1B202E] text-[#9AA3B8] hover:text-white'
                    }`}
                  >
                    {opt.label}
                  </button>
                ))}
              </div>
              <div className="flex gap-2 pt-1">
                <button
                  type="button"
                  onClick={onToggleCrt}
                  className={`flex-1 rounded border px-2.5 py-1.5 text-xs font-medium whitespace-nowrap transition-colors ${
                    crtEnabled
                      ? 'border-[#4EA5D9] bg-[#4EA5D9]/15 text-[#DDF5FF]'
                      : 'border-[#2A3142] bg-[#1B202E] text-[#9AA3B8]'
                  }`}
                >
                  CRT Scanlines: {crtEnabled ? 'On' : 'Off'}
                </button>
                <button
                  type="button"
                  onClick={() => onUpdateTuning({ showHitboxes: !tuning.showHitboxes })}
                  className={`flex-1 rounded border px-2.5 py-1.5 text-xs font-medium whitespace-nowrap transition-colors ${
                    tuning.showHitboxes
                      ? 'border-[#4EA5D9] bg-[#4EA5D9]/15 text-[#DDF5FF]'
                      : 'border-[#2A3142] bg-[#1B202E] text-[#9AA3B8]'
                  }`}
                >
                  Hitboxes: {tuning.showHitboxes ? 'On' : 'Off'}
                </button>
              </div>
            </div>

            {/* Combat & Movement Feel Sliders */}
            <div className="space-y-3 border-t border-[#262C3C] pt-4">
              <div className="flex items-center justify-between">
                <span className="text-xs font-medium text-[#C8CFDE]">Combat &amp; Movement Registers</span>
                <button
                  type="button"
                  onClick={() => onUpdateTuning(DEFAULT_TUNING)}
                  className="flex items-center gap-1 text-xs text-[#9AA3B8] hover:text-[#F5B841]"
                >
                  <RotateCcw className="h-3 w-3" />
                  Reset
                </button>
              </div>

              <TuningSlider
                label="Crit Hitstop"
                value={tuning.hitstopCritFrames}
                unit="f"
                min={0}
                max={14}
                onChange={(v) => onUpdateTuning({ hitstopCritFrames: v })}
              />
              <TuningSlider
                label="Timed-Hit Window"
                value={tuning.timedHitWindowFrames}
                unit="f"
                min={4}
                max={14}
                onChange={(v) => onUpdateTuning({ timedHitWindowFrames: v })}
              />
              <TuningSlider
                label="Parry Window"
                value={tuning.parryWindowFrames}
                unit="f"
                min={4}
                max={14}
                onChange={(v) => onUpdateTuning({ parryWindowFrames: v })}
              />
              <TuningSlider
                label="Walk Speed (16.8 FP)"
                value={tuning.walkSpeedFp}
                unit="fp"
                min={240}
                max={540}
                step={16}
                onChange={(v) => onUpdateTuning({ walkSpeedFp: v })}
              />
              <TuningSlider
                label="Dash Speed (16.8 FP)"
                value={tuning.dashSpeedFp}
                unit="fp"
                min={420}
                max={780}
                step={16}
                onChange={(v) => onUpdateTuning({ dashSpeedFp: v })}
              />
              <TuningSlider
                label="Pip Follow Distance"
                value={tuning.followDistPx}
                unit="px"
                min={12}
                max={42}
                step={2}
                onChange={(v) => onUpdateTuning({ followDistPx: v })}
              />
            </div>

            {/* Sandbox Test Shortcuts */}
            <div className="space-y-2 border-t border-[#262C3C] pt-4">
              <div className="text-xs font-medium text-[#C8CFDE]">Instant Encounter &amp; Field Tools</div>
              <div className="grid grid-cols-2 gap-2">
                <button
                  type="button"
                  onClick={() => onJumpBattle('MEADOW_PACK')}
                  className="flex items-center justify-center gap-1.5 rounded border border-[#2F374A] bg-[#1E2434] px-3 py-2 text-xs font-medium text-[#ECE7DC] hover:border-[#D4942A]"
                >
                  <Swords className="h-3.5 w-3.5 text-[#F5B841]" />
                  Meadow Pack
                </button>
                <button
                  type="button"
                  onClick={() => onJumpBattle('SHRINE_BOSS')}
                  className="flex items-center justify-center gap-1.5 rounded border border-[#2F374A] bg-[#1E2434] px-3 py-2 text-xs font-medium text-[#ECE7DC] hover:border-[#E04E4E]"
                >
                  <Swords className="h-3.5 w-3.5 text-[#E04E4E]" />
                  Shrine Boss
                </button>
                <button
                  type="button"
                  onClick={onRespawnEnemies}
                  className="rounded border border-[#2F374A] bg-[#1B202E] px-3 py-1.5 text-xs font-medium text-[#C8CFDE] hover:text-white"
                >
                  Respawn Field Foes
                </button>
                <button
                  type="button"
                  onClick={onHealParty}
                  className="rounded border border-[#2F374A] bg-[#1B202E] px-3 py-1.5 text-xs font-medium text-[#C8CFDE] hover:text-white"
                >
                  Full Heal Party
                </button>
              </div>
            </div>
          </div>
        )}

        {tab === 'assets' && (
          <div className="space-y-3" data-version={assetVersion}>
            <p className="text-xs leading-relaxed text-[#9AA3B8]">
              Drop your own PNG sprites or tiles into any slot to live-override the Web Host atlas
              without modifying engine core code.
            </p>
            <div className="space-y-2">
              {SWAPPABLE_SLOTS.map((slot) => {
                const preview = atlas.getSlotPreviewDataUrl(slot.id);
                const isOverridden = atlas.hasOverride(slot.id);
                return (
                  <div
                    key={slot.id}
                    className="flex items-center justify-between gap-3 rounded-md border border-[#262C3C] bg-[#12151E] p-2.5"
                  >
                    <div className="flex items-center gap-3 min-w-0">
                      <div className="flex h-10 w-10 shrink-0 items-center justify-center rounded border border-[#2C3346] bg-[#1D2230]">
                        {preview && (
                          <img
                            src={preview}
                            alt={slot.label}
                            className="pixelated h-8 w-8 object-contain"
                          />
                        )}
                      </div>
                      <div className="min-w-0">
                        <div className="truncate text-xs font-medium text-[#F4EFE4]">
                          {slot.label}
                        </div>
                        <div className="font-mono text-[11px] text-[#838DA3]">
                          {isOverridden ? 'Custom PNG loaded' : slot.spec}
                        </div>
                      </div>
                    </div>
                    <div className="flex items-center gap-1.5 shrink-0">
                      <label className="flex cursor-pointer items-center gap-1 rounded border border-[#31394E] bg-[#1E2434] px-2.5 py-1.5 text-xs font-medium text-[#ECE7DC] hover:border-[#D4942A]">
                        <Upload className="h-3 w-3" />
                        Swap
                        <input
                          type="file"
                          accept="image/png,image/webp,image/gif"
                          className="hidden"
                          onChange={(e) => handleUploadAsset(slot.id, e.target.files?.[0])}
                        />
                      </label>
                      {isOverridden && (
                        <button
                          type="button"
                          onClick={() => handleResetAsset(slot.id)}
                          className="rounded border border-[#31394E] p-1.5 text-[#9AA3B8] hover:text-white"
                          title="Restore default pixel art"
                        >
                          <RotateCcw className="h-3 w-3" />
                        </button>
                      )}
                    </div>
                  </div>
                );
              })}
            </div>
          </div>
        )}

        {tab === 'c99' && (
          <div className="flex h-full flex-col space-y-3">
            <p className="text-xs leading-relaxed text-[#9AA3B8]">
              Export exact C99 structs, live feel registers, hardware-packed palettes, tilemaps, and
              cutscene bytecode ready for retro console or SDL2 toolchains.
            </p>

            {/* Target Platform Selector */}
            <div className="grid grid-cols-3 gap-1">
              {(
                [
                  { id: 'SGDK_GENESIS', label: 'Genesis' },
                  { id: 'PVSNESLIB_SNES', label: 'SNES' },
                  { id: 'SDL2_C99', label: 'SDL2 C99' },
                ] as { id: C99Target; label: string }[]
              ).map((t) => (
                <button
                  key={t.id}
                  type="button"
                  onClick={() => setC99Target(t.id)}
                  className={`rounded border px-2 py-1.5 text-xs font-medium whitespace-nowrap ${
                    c99Target === t.id
                      ? 'border-[#D4942A] bg-[#D4942A]/15 text-[#FFF1C4]'
                      : 'border-[#2A3142] bg-[#1B202E] text-[#9AA3B8]'
                  }`}
                >
                  {t.label}
                </button>
              ))}
            </div>

            {/* File Switcher + Actions */}
            <div className="flex items-center justify-between gap-2">
              <div className="flex rounded border border-[#2A3142] bg-[#11141C] p-0.5 text-xs">
                <button
                  type="button"
                  onClick={() => setC99File('types')}
                  className={`rounded px-2 py-1 font-mono text-[11px] ${
                    c99File === 'types' ? 'bg-[#262D40] text-white' : 'text-[#8F98AE]'
                  }`}
                >
                  hearth_types.h
                </button>
                <button
                  type="button"
                  onClick={() => setC99File('data')}
                  className={`rounded px-2 py-1 font-mono text-[11px] ${
                    c99File === 'data' ? 'bg-[#262D40] text-white' : 'text-[#8F98AE]'
                  }`}
                >
                  hearth_data.h
                </button>
              </div>
              <div className="flex items-center gap-1.5">
                <button
                  type="button"
                  onClick={handleCopyCode}
                  className="flex items-center gap-1 rounded border border-[#2F374A] bg-[#1E2434] px-2.5 py-1 text-xs font-medium text-[#ECE7DC] hover:border-[#D4942A]"
                >
                  {copied ? <Check className="h-3.5 w-3.5 text-[#74B461]" /> : <Copy className="h-3.5 w-3.5" />}
                  {copied ? 'Copied' : 'Copy'}
                </button>
                <button
                  type="button"
                  onClick={handleDownloadHeader}
                  className="flex items-center gap-1 rounded bg-[#D4942A] px-2.5 py-1 text-xs font-semibold text-[#161923] hover:bg-[#E4A73D]"
                >
                  <Download className="h-3.5 w-3.5" />
                  .h
                </button>
              </div>
            </div>

            <pre className="max-h-[420px] flex-1 overflow-auto rounded-md border border-[#262C3C] bg-[#0D1017] p-3 font-mono text-[11px] leading-relaxed text-[#C9D2E3]">
              <code>{generatedCode}</code>
            </pre>
          </div>
        )}
      </div>
    </aside>
  );
};

interface TuningSliderProps {
  label: string;
  value: number;
  unit: string;
  min: number;
  max: number;
  step?: number;
  onChange: (val: number) => void;
}

const TuningSlider: React.FC<TuningSliderProps> = ({
  label,
  value,
  unit,
  min,
  max,
  step = 1,
  onChange,
}) => (
  <label className="block space-y-1">
    <div className="flex items-center justify-between text-xs">
      <span className="text-[#AEB6C8]">{label}</span>
      <span className="font-mono font-medium tabular-nums text-[#F5B841]">
        {value} {unit}
      </span>
    </div>
    <input
      type="range"
      min={min}
      max={max}
      step={step}
      value={value}
      onChange={(e) => onChange(Number(e.target.value))}
      className="h-1.5 w-full cursor-pointer appearance-none rounded-lg bg-[#262D40] accent-[#D4942A]"
    />
  </label>
);
