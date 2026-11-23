import React, { useEffect, useState } from 'react';
import { PortraitId } from '../core/types';
import portraitRowan from '../assets/images/portrait_rowan_1795315522922.jpg';
import portraitMara from '../assets/images/portrait_grandma_mara_1795315523861.jpg';
import portraitPip from '../assets/images/portrait_pip_gnome_1795404214797.jpg';
import portraitBarnaby from '../assets/images/portrait_barnaby_1795315522478.jpg';
import portraitMaple from '../assets/images/portrait_maple_1795315522927.jpg';
import portraitJasper from '../assets/images/portrait_jasper_1795315522636.jpg';
import portraitMido from '../assets/images/portrait_mido_1795319777822.jpg';
import portraitTessa from '../assets/images/portrait_tessa_1795319779391.jpg';
import portraitCorin from '../assets/images/portrait_corin_1795319780579.jpg';
import portraitSentinel from '../assets/images/portrait_sentinel_1795319781052.jpg';
import portraitElder from '../assets/images/portrait_villager_elder_1794938563121.jpg';

const PAINTED_PORTRAITS_BY_ID: Record<number, string> = {
  1: portraitRowan,
  2: portraitMara,
  3: portraitPip,
  4: portraitBarnaby,
  5: portraitMaple,
  6: portraitJasper,
  7: portraitMido,
  8: portraitTessa,
  9: portraitCorin,
  10: portraitSentinel,
};

const GENERATED_PORTRAITS: Partial<Record<PortraitId, string>> = {
  hero: portraitRowan,
  pip: portraitPip,
  elder: portraitElder,
};

interface PortraitFrameProps {
  portrait: PortraitId;
  portraitId?: number;
  speaker: string;
}

export const PortraitFrame: React.FC<PortraitFrameProps> = ({ portrait, portraitId = 0, speaker }) => {
  const [imgFailed, setImgFailed] = useState(false);

  useEffect(() => {
    setImgFailed(false);
  }, [portrait, portraitId]);

  const src = (portraitId > 0 ? PAINTED_PORTRAITS_BY_ID[portraitId] : undefined) ?? GENERATED_PORTRAITS[portrait];
  if (portrait === 'none' && !src) return null;

  const usePhoto = Boolean(src) && !imgFailed;

  return (
    <div className="relative h-20 w-20 shrink-0 overflow-hidden rounded-[2px] border-2 border-[#B38E4E] bg-[#1A1C28] shadow-inner transition-opacity duration-150 sm:h-22 sm:w-22">
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

/** Hand-painted stylized vector portrait fallback for Clara, Guard Bram, Mousefolk, Boarfolk, Shadow Grandma, or image errors */
const StylizedFallbackPortrait: React.FC<{ portrait: PortraitId; speaker: string }> = ({
  portrait,
  speaker,
}) => {
  if (portrait === 'mouse') {
    return (
      <div className="flex h-full w-full items-center justify-center bg-[#2E2634]" aria-label={speaker}>
        <svg viewBox="0 0 64 64" className="h-full w-full">
          <rect width="64" height="64" fill="#2E2634" />
          <circle cx="17" cy="20" r="10" fill="#B8B2BA" stroke="#1B181E" strokeWidth="2" />
          <circle cx="17" cy="20" r="6" fill="#E6A4A8" />
          <circle cx="47" cy="20" r="10" fill="#B8B2BA" stroke="#1B181E" strokeWidth="2" />
          <circle cx="47" cy="20" r="6" fill="#E6A4A8" />
          <circle cx="32" cy="60" r="22" fill="#A63A3A" stroke="#1B181E" strokeWidth="2" />
          <path d="M23 44 L32 52 L41 44" fill="none" stroke="#F4E7C8" strokeWidth="3" />
          <ellipse cx="32" cy="30" rx="14" ry="13" fill="#CFC9D0" stroke="#1B181E" strokeWidth="2" />
          <circle cx="26" cy="28" r="2" fill="#1B181E" />
          <circle cx="38" cy="28" r="2" fill="#1B181E" />
          <ellipse cx="32" cy="34" rx="3" ry="2.2" fill="#D46A7E" />
          <path d="M16 33 L24 33 M17 36 L24 35 M40 33 L48 33 M40 35 L47 36" stroke="#4D4852" strokeWidth="1.2" />
        </svg>
      </div>
    );
  }

  if (portrait === 'pig') {
    return (
      <div className="flex h-full w-full items-center justify-center bg-[#362222]" aria-label={speaker}>
        <svg viewBox="0 0 64 64" className="h-full w-full">
          <rect width="64" height="64" fill="#362222" />
          <polygon points="14,12 24,20 16,27" fill="#E6A4A0" stroke="#1B181E" strokeWidth="2" />
          <polygon points="50,12 40,20 48,27" fill="#E6A4A0" stroke="#1B181E" strokeWidth="2" />
          <circle cx="32" cy="60" r="22" fill="#5D6678" stroke="#1B181E" strokeWidth="2" />
          <path d="M20 45 L32 52 L44 45" fill="none" stroke="#D4A454" strokeWidth="3" />
          <circle cx="32" cy="29" r="15" fill="#F0B5AD" stroke="#1B181E" strokeWidth="2" />
          <circle cx="26" cy="25" r="2" fill="#1B181E" />
          <circle cx="38" cy="25" r="2" fill="#1B181E" />
          <ellipse cx="32" cy="33" rx="7" ry="5" fill="#DC8E88" stroke="#1B181E" strokeWidth="1.8" />
          <circle cx="29.5" cy="33" r="1.4" fill="#1B181E" />
          <circle cx="34.5" cy="33" r="1.4" fill="#1B181E" />
        </svg>
      </div>
    );
  }

  if (portrait === 'shadow_gran') {
    return (
      <div className="flex h-full w-full items-center justify-center bg-[#181024]" aria-label={speaker}>
        <svg viewBox="0 0 64 64" className="h-full w-full">
          <rect width="64" height="64" fill="#181024" />
          <circle cx="32" cy="60" r="23" fill="#2A1D3D" stroke="#0C0814" strokeWidth="2" />
          <path d="M12 48 Q32 4 52 48 Z" fill="#231634" stroke="#0C0814" strokeWidth="2" />
          <circle cx="32" cy="29" r="12" fill="#3B2B4E" stroke="#0C0814" strokeWidth="2" />
          <path d="M20 26 Q32 14 44 26" fill="none" stroke="#B6AEC8" strokeWidth="3.5" />
          <ellipse cx="27" cy="29" rx="2.4" ry="1.6" fill="#E59CFF" />
          <ellipse cx="37" cy="29" rx="2.4" ry="1.6" fill="#E59CFF" />
          <path d="M27 36 Q32 34 37 36" fill="none" stroke="#160E20" strokeWidth="1.8" />
        </svg>
      </div>
    );
  }

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
