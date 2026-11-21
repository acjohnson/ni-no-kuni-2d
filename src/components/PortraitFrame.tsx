import React, { useState } from 'react';
import { PortraitId } from '../core/types';

const GENERATED_PORTRAITS: Partial<Record<PortraitId, string>> = {
  hero: '/src/assets/images/portrait_hero_1794938562548.jpg',
  pip: '/src/assets/images/portrait_lantern_sprite_1794938563176.jpg',
  elder: '/src/assets/images/portrait_villager_elder_1794938563121.jpg',
};

interface PortraitFrameProps {
  portrait: PortraitId;
  speaker: string;
}

export const PortraitFrame: React.FC<PortraitFrameProps> = ({ portrait, speaker }) => {
  const [imgFailed, setImgFailed] = useState(false);

  if (portrait === 'none') return null;

  const src = GENERATED_PORTRAITS[portrait];
  const usePhoto = Boolean(src) && !imgFailed;

  return (
    <div className="relative h-20 w-20 shrink-0 overflow-hidden rounded-[2px] border-2 border-[#9B7A45] bg-[#1E202C] shadow-inner sm:h-22 sm:w-22">
      {usePhoto && src ? (
        <img
          src={src}
          alt={speaker}
          referrerPolicy="no-referrer"
          onError={() => setImgFailed(true)}
          className="h-full w-full object-cover"
        />
      ) : (
        <StylizedFallbackPortrait portrait={portrait} speaker={speaker} />
      )}
    </div>
  );
};

/** Hand-painted stylized vector portrait fallback for Clara, Guard Bram, or image errors */
const StylizedFallbackPortrait: React.FC<{ portrait: PortraitId; speaker: string }> = ({
  portrait,
  speaker,
}) => {
  const palette =
    portrait === 'clara'
      ? { bg: '#3E2D38', hair: '#E8B852', tunic: '#8C3B58', accent: '#F4E7C8' }
      : portrait === 'guard'
        ? { bg: '#222F44', hair: '#68432E', tunic: '#385888', accent: '#C4CBD4' }
        : portrait === 'pip'
          ? { bg: '#2F251A', hair: '#D4942A', tunic: '#B84333', accent: '#FFF1A8' }
          : { bg: '#26342C', hair: '#6B3E26', tunic: '#2F6E49', accent: '#B84333' };

  return (
    <div
      className="flex h-full w-full flex-col items-center justify-center"
      style={{ backgroundColor: palette.bg }}
      aria-label={speaker}
    >
      <svg viewBox="0 0 64 64" className="h-full w-full">
        <rect width="64" height="64" fill={palette.bg} />
        <circle cx="32" cy="60" r="22" fill={palette.tunic} stroke="#1B181E" strokeWidth="2" />
        <path d="M22 44 L32 52 L42 44" fill="none" stroke={palette.accent} strokeWidth="3" />
        <circle cx="32" cy="27" r="14" fill="#F3D1B0" stroke="#1B181E" strokeWidth="2" />
        <path
          d="M17 26 C18 11, 46 11, 47 26 C42 20, 22 20, 17 26 Z"
          fill={palette.hair}
          stroke="#1B181E"
          strokeWidth="2"
        />
        <circle cx="27" cy="28" r="1.8" fill="#1B181E" />
        <circle cx="37" cy="28" r="1.8" fill="#1B181E" />
        <path
          d="M28 34 Q32 37 36 34"
          fill="none"
          stroke="#1B181E"
          strokeWidth="1.8"
          strokeLinecap="round"
        />
      </svg>
    </div>
  );
};
