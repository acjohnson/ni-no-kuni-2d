import React, { useEffect, useRef, useState } from 'react';
import { IntroState } from '../core/types';
import cgAlderbrookDusk from '../assets/images/cinema_alderbrook_dusk_1795309262024.jpg';
import cgHollowOakChase from '../assets/images/cinema_hollow_oak_chase_1795309261000.jpg';
import cgEvertaleVista from '../assets/images/cinema_evertale_panorama_1795309261863.jpg';
import cgShadowGrandma from '../assets/images/cinema_shadow_grandma_1795309261174.jpg';
import cgBedroomComfort from '../assets/images/cinema_bedroom_comfort_v2_1795309287400.jpg';
import cgPipWindow from '../assets/images/cinema_pip_window_1795309260734.jpg';
import cgAtticKeepsake from '../assets/images/cg_attic_keepsake_1795315522510.jpg';
import cgPipDreamReturn from '../assets/images/cg_pip_dream_return_1795315522510.jpg';
import cgHollowOakAwakens from '../assets/images/cg_hollow_oak_awakens_1795315522718.jpg';
import cgEvertaleWorldVista from '../assets/images/cg_evertale_world_vista_1795319776319.jpg';

const CG_PLATES: readonly { src: string; alt: string; fallbackBg: string }[] = [
  {
    src: cgAlderbrookDusk,
    alt: 'Alderbrook mountain rail town at golden dusk',
    fallbackBg: 'linear-gradient(180deg, #1b2242 0%, #5c3f57 55%, #232d2b 100%)',
  },
  {
    src: cgHollowOakChase,
    alt: 'Young Rowan chasing Milo toward the glowing Hollow Oak in Whispering Woods',
    fallbackBg: 'linear-gradient(180deg, #122126 0%, #1d3c34 55%, #3c4d2c 100%)',
  },
  {
    src: cgEvertaleVista,
    alt: 'Sunlit storybook valley of Evertale with windmills and floating paper lanterns',
    fallbackBg: 'linear-gradient(180deg, #27588c 0%, #63a4c7 50%, #4d7b46 100%)',
  },
  {
    src: cgShadowGrandma,
    alt: 'Shadow Grandma emerging beneath a violet eclipse in Evertale',
    fallbackBg: 'linear-gradient(180deg, #130b24 0%, #37154a 55%, #190d26 100%)',
  },
  {
    src: cgBedroomComfort,
    alt: 'Grandma Mara comforting five-year-old Rowan in his warm timber bedroom',
    fallbackBg: 'linear-gradient(180deg, #261914 0%, #5e3b24 55%, #231610 100%)',
  },
  {
    src: cgPipWindow,
    alt: 'Pip the lantern sprite glowing outside Rowan’s frosted bedroom window at midnight',
    fallbackBg: 'linear-gradient(180deg, #0d1633 0%, #1d3566 55%, #121c33 100%)',
  },
  {
    src: cgAtticKeepsake,
    alt: 'Rowan opening Grandma Mara’s cedar attic chest to discover Milo’s brass bell and a pressed golden oak leaf',
    fallbackBg: 'linear-gradient(180deg, #2b1d14 0%, #6a4424 52%, #241812 100%)',
  },
  {
    src: cgPipDreamReturn,
    alt: 'Pip the glowing lantern sprite calling out to Rowan in a moonlit mountain dream above autumn ridges',
    fallbackBg: 'linear-gradient(180deg, #0b1430 0%, #1f3d73 55%, #131c36 100%)',
  },
  {
    src: cgHollowOakAwakens,
    alt: 'Rowan and Mido standing before the colossal Hollow Oak as a golden doorway into Evertale opens',
    fallbackBg: 'linear-gradient(180deg, #13212c 0%, #403628 52%, #1d2a20 100%)',
  },
  {
    src: cgEvertaleWorldVista,
    alt: 'Sweeping sunlit panorama of the Evertale Continent with hilltop Sunhill Market, white spires of Castle Aurelia, and winding rivers',
    fallbackBg: 'linear-gradient(180deg, #1d5794 0%, #6eb5d8 48%, #4b8644 100%)',
  },
];

interface CinemaTheaterProps {
  intro: IntroState;
  fadeLevel: number;
  flashAlpha: number;
  onAdvance: () => void;
  onToggleAuto: () => void;
  onSkip: () => void;
}

export const CinemaTheater: React.FC<CinemaTheaterProps> = ({
  intro,
  fadeLevel,
  flashAlpha,
  onAdvance,
  onToggleAuto,
  onSkip,
}) => {
  const fxCanvasRef = useRef<HTMLCanvasElement | null>(null);
  const fxModeRef = useRef<number>(intro.fxMode);
  fxModeRef.current = intro.fxMode;
  const [failedPlateIdx, setFailedPlateIdx] = useState<number | null>(null);

  /* Lightweight 60fps atmospheric particle canvas layered over the painted CG plate */
  useEffect(() => {
    const canvas = fxCanvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const particles = Array.from({ length: 34 }, (_, i) => ({
      x: ((i * 73) % 640),
      y: ((i * 41) % 300),
      r: 1.2 + (i % 4) * 0.7,
      vx: ((i % 5) - 2) * 0.16,
      vy: -0.18 - (i % 4) * 0.12,
      phase: i * 0.7,
    }));

    let rafId = 0;
    let tick = 0;
    const draw = () => {
      tick++;
      const w = canvas.width;
      const h = canvas.height;
      ctx.clearRect(0, 0, w, h);
      const mode = fxModeRef.current;

      for (const p of particles) {
        if (mode === 0) {
          /* Steam wisps & warm twilight dust motes */
          p.x = (p.x + 0.25 + p.vx * 0.5 + w) % w;
          p.y = (p.y + p.vy * 0.75 + h) % h;
          const a = 0.16 + 0.12 * Math.sin(tick * 0.03 + p.phase);
          ctx.fillStyle = `rgba(255, 224, 178, ${a.toFixed(3)})`;
          ctx.beginPath();
          ctx.arc(p.x, p.y, p.r * 1.4, 0, Math.PI * 2);
          ctx.fill();
        } else if (mode === 1) {
          /* Golden Whispering Woods pollen */
          p.x = (p.x + Math.sin(tick * 0.02 + p.phase) * 0.35 + w) % w;
          p.y = (p.y + p.vy * 0.55 + h) % h;
          const a = 0.28 + 0.22 * Math.sin(tick * 0.05 + p.phase);
          ctx.fillStyle = `rgba(255, 240, 155, ${a.toFixed(3)})`;
          ctx.beginPath();
          ctx.arc(p.x, p.y, p.r, 0, Math.PI * 2);
          ctx.fill();
        } else if (mode === 2) {
          /* Floating sunlit Evertale lantern embers */
          p.x = (p.x + Math.sin(tick * 0.018 + p.phase) * 0.4 + 0.15 + w) % w;
          p.y = (p.y - 0.32 - (p.r * 0.08) + h) % h;
          const a = 0.35 + 0.25 * Math.sin(tick * 0.06 + p.phase);
          ctx.fillStyle = `rgba(255, 212, 118, ${a.toFixed(3)})`;
          ctx.beginPath();
          ctx.arc(p.x, p.y, p.r * 1.25, 0, Math.PI * 2);
          ctx.fill();
        } else if (mode === 3) {
          /* Violet eclipse storm sparks */
          p.x = (p.x - 0.85 + p.vx + w) % w;
          p.y = (p.y + 0.45 + p.vy * 0.4 + h) % h;
          const a = 0.28 + 0.22 * Math.sin(tick * 0.09 + p.phase);
          ctx.fillStyle = `rgba(196, 116, 255, ${a.toFixed(3)})`;
          ctx.beginPath();
          ctx.arc(p.x, p.y, p.r * 1.1, 0, Math.PI * 2);
          ctx.fill();
        } else {
          /* Midnight moonlit fireflies (Pip's window) */
          p.x = (p.x + Math.sin(tick * 0.025 + p.phase) * 0.45 + w) % w;
          p.y = (p.y + Math.cos(tick * 0.02 + p.phase) * 0.28 + h) % h;
          const a = 0.25 + 0.4 * Math.max(0, Math.sin(tick * 0.055 + p.phase));
          ctx.fillStyle = `rgba(255, 241, 168, ${a.toFixed(3)})`;
          ctx.beginPath();
          ctx.arc(p.x, p.y, p.r * 1.35, 0, Math.PI * 2);
          ctx.fill();
        }
      }

      rafId = window.requestAnimationFrame(draw);
    };

    rafId = window.requestAnimationFrame(draw);
    return () => window.cancelAnimationFrame(rafId);
  }, []);

  const plateIdx = Math.max(0, Math.min(CG_PLATES.length - 1, intro.scene));
  const plate = CG_PLATES[plateIdx];

  /* Slow, graceful Ken Burns camera motion driven deterministically by plateTimer (0..1200+) */
  const t = Math.min(1, intro.plateTimer / 900);
  let scale = 1.04;
  let tx = 0;
  let ty = 0;
  if (intro.camMode === 0) {
    /* CIN_CAM_PAN_RIGHT */
    scale = 1.07 + t * 0.03;
    tx = -2.8 + t * 5.6;
    ty = 0.5 - t * 1.0;
  } else if (intro.camMode === 1) {
    /* CIN_CAM_ZOOM_IN */
    scale = 1.02 + t * 0.09;
    tx = 0;
    ty = -t * 1.5;
  } else if (intro.camMode === 2) {
    /* CIN_CAM_PAN_LEFT */
    scale = 1.07 + t * 0.03;
    tx = 2.6 - t * 5.2;
    ty = 0;
  } else if (intro.camMode === 3) {
    /* CIN_CAM_SHAKE_FLASH: creeping dread zoom */
    scale = 1.04 + t * 0.08;
    tx = 0;
    ty = -0.8;
  } else {
    /* CIN_CAM_STILL_DAWN: gentle pull-out at dawn */
    scale = 1.09 - t * 0.05;
    tx = 0;
    ty = 0;
  }

  const isDawnReprise = intro.camMode === 4;
  const isEclipse = intro.camMode === 3;
  const fullL1 = intro.line1 || '';
  const fullL2 = intro.line2 || '';
  const totalChars = fullL1.length + fullL2.length;
  const vis = intro.chars;
  const shownL1 = fullL1.slice(0, Math.min(vis, fullL1.length));
  const shownL2 = vis > fullL1.length ? fullL2.slice(0, vis - fullL1.length) : '';
  const finishedTyping = vis >= totalChars && fadeLevel <= 2;

  const speakerColor =
    intro.speaker === 'SHADOW GRANDMA'
      ? 'text-[#D896FF]'
      : intro.speaker === 'PIP'
        ? 'text-[#FFE680]'
        : intro.speaker === 'GRANDMA MARA'
          ? 'text-[#F6C87A]'
          : 'text-[#8CD9FF]';

  return (
    <div
      className="absolute inset-0 z-20 flex flex-col bg-[#08070F] select-none overflow-hidden"
      onClick={onAdvance}
    >
      {/* TOP LETTERBOX BAR: Act Title + Unboxed 7-Act Progress Markers + Controls */}
      <div className="flex shrink-0 items-center justify-between border-b border-[#6E552E]/70 bg-[#0C0A14] px-3.5 py-2 sm:px-5">
        <div className="flex items-center gap-3 min-w-0">
          <span className="font-pixel text-[10px] tracking-[0.14em] text-[#F5D688] truncate sm:text-[11px]">
            {intro.title || (intro.scriptId === 0 ? 'PROLOGUE' : 'CHAPTER I')}
          </span>
          {intro.scriptId === 0 ? (
            <div className="hidden items-center gap-1 sm:flex" aria-label={`Act ${intro.actNum} of 7`}>
              {[1, 2, 3, 4, 5, 6, 7].map((act) => (
                <span
                  key={act}
                  className={
                    act === intro.actNum
                      ? 'h-1.5 w-4 bg-[#F5D688] transition-all duration-300'
                      : act < intro.actNum
                        ? 'h-1.5 w-1.5 bg-[#8E7342]'
                        : 'h-1.5 w-1.5 bg-[#312B3D]'
                  }
                />
              ))}
            </div>
          ) : (
            <div className="hidden items-center gap-1 sm:flex" aria-label={`Beat ${intro.beat + 1} of ${intro.beatCount}`}>
              {Array.from({ length: Math.min(8, intro.beatCount) }, (_, idx) => (
                <span
                  key={idx}
                  className={
                    idx === intro.beat
                      ? 'h-1.5 w-4 bg-[#F5D688] transition-all duration-300'
                      : idx < intro.beat
                        ? 'h-1.5 w-1.5 bg-[#8E7342]'
                        : 'h-1.5 w-1.5 bg-[#312B3D]'
                  }
                />
              ))}
            </div>
          )}
        </div>

        <div className="flex items-center gap-2 shrink-0">
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation();
              onToggleAuto();
            }}
            className={`font-pixel border px-2 py-0.5 text-[9px] tracking-wider transition-colors ${
              intro.autoPlay
                ? 'border-[#F5D688] bg-[#3A2E1B] text-[#FFF3C4]'
                : 'border-[#3E374C] bg-[#161322] text-[#9E98AD] hover:border-[#78688F] hover:text-[#E8E2F4]'
            }`}
          >
            AUTO: {intro.autoPlay ? 'ON' : 'OFF'}
          </button>
          <button
            type="button"
            onClick={(e) => {
              e.stopPropagation();
              onSkip();
            }}
            className="font-pixel border border-[#4D3D24] bg-[#171320] px-2 py-0.5 text-[9px] tracking-wider text-[#E6C57A] transition-colors hover:border-[#F5D688] hover:text-[#FFF6DC]"
          >
            SKIP &gt;&gt;
          </button>
        </div>
      </div>

      {/* CENTER PAINTED ANIME CG VIEWPORT */}
      <div
        className="relative flex-1 overflow-hidden"
        style={{ background: plate.fallbackBg }}
      >
        {failedPlateIdx !== plateIdx && (
          <img
            key={plateIdx}
            src={plate.src}
            alt={plate.alt}
            referrerPolicy="no-referrer"
            draggable={false}
            onError={() => setFailedPlateIdx(plateIdx)}
            className="h-full w-full object-cover will-change-transform"
            style={{
              transform: `scale(${scale.toFixed(4)}) translate(${tx.toFixed(2)}%, ${ty.toFixed(2)}%)`,
              filter: isDawnReprise
                ? 'brightness(1.12) saturate(1.15) hue-rotate(-8deg)'
                : isEclipse
                  ? 'contrast(1.08) saturate(1.12)'
                  : 'none',
            }}
          />
        )}

        {/* Atmospheric particles layer */}
        <canvas
          ref={fxCanvasRef}
          width={640}
          height={300}
          className="pointer-events-none absolute inset-0 h-full w-full"
        />

        {/* Subtle cinematic vignette framing */}
        <div
          className="pointer-events-none absolute inset-0"
          style={{
            boxShadow: 'inset 0 0 42px rgba(6, 5, 12, 0.72)',
          }}
        />

        {/* Dramatic white/violet lightning flash */}
        {flashAlpha > 0.01 && (
          <div
            className="pointer-events-none absolute inset-0"
            style={{
              backgroundColor: isEclipse
                ? `rgba(218, 165, 255, ${(flashAlpha * 0.75).toFixed(3)})`
                : `rgba(255, 250, 232, ${flashAlpha.toFixed(3)})`,
            }}
          />
        )}

        {/* Hardware-synchronized plate crossfade veil (0..15) */}
        {fadeLevel > 0 && (
          <div
            className="pointer-events-none absolute inset-0 bg-[#07060C]"
            style={{ opacity: Math.min(1, fadeLevel / 15) }}
          />
        )}
      </div>

      {/* BOTTOM STORYBOOK NARRATIVE BAR */}
      <div className="shrink-0 border-t border-[#8B6B38] bg-[#0D0B15] px-4 py-3 sm:px-6 sm:py-3.5">
        <div className="mx-auto flex max-w-2xl flex-col justify-between min-h-[72px] sm:min-h-[82px]">
          <div className="flex items-center justify-between gap-2 pb-1">
            {intro.speaker ? (
              <span className={`font-pixel text-[11px] font-bold tracking-[0.12em] ${speakerColor}`}>
                {intro.speaker}
              </span>
            ) : (
              <span className="font-pixel text-[9px] tracking-[0.16em] text-[#8E836D]">
                {intro.scriptId === 0 ? 'PROLOGUE' : 'STORY SCENE'} · {intro.beat + 1} / {intro.beatCount}
              </span>
            )}
            <span className="font-pixel text-[9px] tracking-wider text-[#8CC4E6]">
              {finishedTyping ? '[SPACE / CLICK] NEXT' : '...'}
            </span>
          </div>

          <div
            className={`space-y-1 text-[13px] leading-relaxed sm:text-[15px] ${
              intro.speaker
                ? 'font-sans font-medium text-[#FAF3E3]'
                : 'font-serif italic text-[#EFE4CB]'
            }`}
          >
            <p className="min-h-[1.4em]">{shownL1}</p>
            <p className="min-h-[1.4em] text-[#E3D5B7]">{shownL2}</p>
          </div>
        </div>
      </div>
    </div>
  );
};
