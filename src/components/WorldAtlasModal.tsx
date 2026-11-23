import React, { useEffect, useRef } from 'react';
import { Compass, Navigation, Ship, X } from 'lucide-react';
import { ATLAS_H, ATLAS_W, GlobeLandmark, VEHICLE_NAMES } from '../core/types';

interface WorldAtlasModalProps {
  open: boolean;
  atlasCells: Uint8Array;
  landmarks: readonly GlobeLandmark[];
  regionNames: readonly string[];
  worldRegionId: number;
  worldRegionName: string;
  activeVehicle: number;
  heroTx: number;
  heroTy: number;
  globeW: number;
  globeH: number;
  designerAllowed: boolean;
  designerSandbox: boolean;
  onClose: () => void;
  onCycleVehicle: () => void;
  onWarpRegion: (regionId: number) => void;
}

const ATLAS_PALETTE: readonly string[] = [
  '#143A64', // 0: Deep Cobalt Ocean
  '#2FA4B8', // 1: Turquoise Shallows (Raftable)
  '#5AD4C8', // 2: Coral Reef Shoals (Raftable)
  '#4F9C4C', // 3: Emerald Heartland
  '#E0B05C', // 4: Sands of Babana
  '#F2D680', // 5: Suncrest Paradise
  '#E8F4FA', // 6: Frostmere North
  '#1E5C32', // 7: Canopy Wilds
  '#B83C24', // 8: Cinderpeak Caldera
  '#74A4C8', // 9: Aetheris Citadel
];

const LEGEND_ITEMS: readonly { label: string; color: string }[] = [
  { label: 'Heartland', color: '#4F9C4C' },
  { label: 'Raft Shallows', color: '#2FA4B8' },
  { label: 'Sands of Babana', color: '#E0B05C' },
  { label: 'Suncrest', color: '#F2D680' },
  { label: 'Frostmere', color: '#E8F4FA' },
  { label: 'Canopy Wilds', color: '#1E5C32' },
  { label: 'Cinderpeak', color: '#B83C24' },
  { label: 'Aetheris', color: '#74A4C8' },
];

export const WorldAtlasModal: React.FC<WorldAtlasModalProps> = ({
  open,
  atlasCells,
  landmarks,
  regionNames,
  worldRegionId,
  worldRegionName,
  activeVehicle,
  heroTx,
  heroTy,
  globeW,
  globeH,
  designerAllowed,
  designerSandbox,
  onClose,
  onCycleVehicle,
  onWarpRegion,
}) => {
  const canvasRef = useRef<HTMLCanvasElement | null>(null);

  useEffect(() => {
    if (!open) return;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.imageSmoothingEnabled = false;

    const scale = 4;
    const w = ATLAS_W * scale;
    const h = ATLAS_H * scale;
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }

    // 1. Paint 64x48 toroidal macro-biome globe cells
    for (let y = 0; y < ATLAS_H; y++) {
      const rowOff = y * ATLAS_W;
      for (let x = 0; x < ATLAS_W; x++) {
        const cell = atlasCells[rowOff + x] ?? 0;
        ctx.fillStyle = ATLAS_PALETTE[cell] ?? '#143A64';
        ctx.fillRect(x * scale, y * scale, scale, scale);
      }
    }

    // 2. Subtle graticule lines emphasizing full toroidal wrap (E-W & N-S)
    ctx.strokeStyle = 'rgba(255, 255, 255, 0.08)';
    ctx.lineWidth = 1;
    for (let gx = 16; gx < ATLAS_W; gx += 16) {
      ctx.beginPath();
      ctx.moveTo(gx * scale + 0.5, 0);
      ctx.lineTo(gx * scale + 0.5, h);
      ctx.stroke();
    }
    for (let gy = 12; gy < ATLAS_H; gy += 12) {
      ctx.beginPath();
      ctx.moveTo(0, gy * scale + 0.5);
      ctx.lineTo(w, gy * scale + 0.5);
      ctx.stroke();
    }

    // 3. Draw 14 continent landmark pins
    for (const lm of landmarks) {
      const lx = Math.round((lm.tx / globeW) * w);
      const ly = Math.round((lm.ty / globeH) * h);
      ctx.fillStyle = '#121520';
      ctx.fillRect(lx - 3, ly - 3, 6, 6);
      ctx.fillStyle = lm.enterable ? '#F5C45A' : '#74D8F5';
      ctx.fillRect(lx - 2, ly - 2, 4, 4);
    }

    // 4. Draw live hero position beacon
    const hx = Math.round(((heroTx % globeW + globeW) % globeW / globeW) * w);
    const hy = Math.round(((heroTy % globeH + globeH) % globeH / globeH) * h);
    ctx.strokeStyle = '#FFFFFF';
    ctx.lineWidth = 1.5;
    ctx.strokeRect(hx - 4.5, hy - 4.5, 9, 9);
    ctx.fillStyle = '#FF4D4D';
    ctx.fillRect(hx - 2, hy - 2, 4, 4);
  }, [open, atlasCells, landmarks, heroTx, heroTy, globeW, globeH]);

  if (!open) return null;

  const vesselLabel = VEHICLE_NAMES[activeVehicle] ?? 'On Foot';

  return (
    <div
      className="absolute inset-0 z-30 flex items-center justify-center bg-[#090B12]/82 p-3"
      onClick={onClose}
    >
      <div
        className="rpg-window flex max-h-full w-full max-w-lg flex-col p-3.5 text-[#ECE7DC]"
        onClick={(e) => e.stopPropagation()}
      >
        {/* Header */}
        <div className="flex items-center justify-between gap-2 border-b border-[#3F3528] pb-2">
          <div className="flex items-center gap-2 min-w-0">
            <Compass className="h-4 w-4 shrink-0 text-[#F5C45A]" />
            <div className="min-w-0">
              <div className="truncate font-pixel text-xs font-bold text-[#FFF3C8]">
                EVERTALE WORLD ATLAS · {worldRegionName.toUpperCase()}
              </div>
              <div className="font-pixel text-[9px] text-[#A8A08E]">
                TOROIDAL GLOBE ({globeW}x{globeH}) · VESSEL: {vesselLabel.toUpperCase()} · POS ({heroTx},{heroTy})
              </div>
            </div>
          </div>
          <button
            type="button"
            onClick={onClose}
            className="rpg-slot p-1 text-[#CFC8B8] hover:text-[#FFF6DC]"
            aria-label="Close World Atlas"
          >
            <X className="h-3.5 w-3.5" />
          </button>
        </div>

        {/* Crisp Pixel Atlas Canvas */}
        <div className="mt-2.5 flex flex-col items-center">
          <div className="relative border-2 border-[#6E552E] bg-[#0B1C34] p-0.5 shadow-inner">
            <canvas
              ref={canvasRef}
              width={ATLAS_W * 4}
              height={ATLAS_H * 4}
              className="pixelated block h-auto w-full max-w-[320px]"
            />
          </div>

          {/* Compact Biome & Pin Legend */}
          <div className="mt-2 flex flex-wrap items-center justify-center gap-x-3 gap-y-1 font-pixel text-[9px] text-[#CFC8B8]">
            {LEGEND_ITEMS.map((item) => (
              <span key={item.label} className="flex items-center gap-1 whitespace-nowrap">
                <span
                  className="inline-block h-2 w-2 border border-[#14121A]"
                  style={{ backgroundColor: item.color }}
                />
                {item.label}
              </span>
            ))}
          </div>
          <div className="mt-1 flex flex-wrap items-center justify-center gap-x-4 font-pixel text-[9px] text-[#9E9584]">
            <span className="flex items-center gap-1">
              <span className="inline-block h-2 w-2 bg-[#F5C45A]" /> Enterable Port / Town
            </span>
            <span className="flex items-center gap-1">
              <span className="inline-block h-2 w-2 bg-[#74D8F5]" /> Frontier Vista
            </span>
            <span className="flex items-center gap-1">
              <span className="inline-block h-2 w-2 border border-white bg-[#FF4D4D]" /> Rowan
            </span>
          </div>
        </div>

        {/* Compile-Time Designer Mode Continent Quick-Jump & Vessel Switcher */}
        {designerAllowed && designerSandbox && (
          <div className="mt-2.5 border-t border-[#3F3528] pt-2">
            <div className="flex items-center justify-between gap-2">
              <span className="flex items-center gap-1 font-pixel text-[9px] font-bold text-[#F5C45A]">
                <Navigation className="h-3 w-3" />
                DESIGNER GLOBE WARP
              </span>
              <button
                type="button"
                onClick={onCycleVehicle}
                className="rpg-slot font-pixel flex items-center gap-1 px-2 py-0.5 text-[9px] text-[#FFF3C8] hover:border-[#F5C45A]"
              >
                <Ship className="h-3 w-3 text-[#6CC8E8]" />
                CYCLE VESSEL ({vesselLabel.toUpperCase()})
              </button>
            </div>
            <div className="mt-1.5 flex flex-wrap gap-1.5">
              {regionNames.map((name, idx) => (
                <button
                  key={name}
                  type="button"
                  onClick={() => onWarpRegion(idx)}
                  className={`font-pixel px-2 py-1 text-[9px] whitespace-nowrap transition-colors ${
                    worldRegionId === idx
                      ? 'rpg-slot-active text-[#FFF4CF]'
                      : 'rpg-slot text-[#CFC8B8] hover:border-[#D4A454]'
                  }`}
                >
                  {name.toUpperCase()}
                </button>
              ))}
            </div>
          </div>
        )}

        <div className="mt-2.5 flex items-center justify-between border-t border-[#3F3528] pt-1.5 font-pixel text-[9px] text-[#8EC8E8]">
          <span>WRAP-AROUND GLOBE · SAIL SHALLOWS ON RAFT TO REACH SUNCREST &amp; BABANA</span>
          <span className="shrink-0 text-[#ABA494]">[M / ESC]: CLOSE</span>
        </div>
      </div>
    </div>
  );
};
