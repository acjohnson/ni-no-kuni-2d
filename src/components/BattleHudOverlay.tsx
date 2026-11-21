import React from 'react';
import { Flame, Shield, Sparkles, Sword, Zap } from 'lucide-react';
import { BattlePhase, FamiliarSlot, LockKind, PartyMemberState } from '../core/types';

interface BattleHudOverlayProps {
  party: PartyMemberState[];
  activeSlot: FamiliarSlot;
  potions: number;
  phase: BattlePhase;
  selectedCommand: number;
  timedRingActive: boolean;
  timedRingKind: 'OFFENSE' | 'PARRY';
  enemies: {
    id: string;
    name: string;
    hp: number;
    maxHp: number;
    alive: boolean;
    isBoss: boolean;
    staggered: boolean;
    locks: LockKind[];
    brokenLocks: boolean[];
  }[];
  selectedEnemy: number;
  onCommand: (cmdIdx: number) => void;
  onSwapFamiliar: (slot: FamiliarSlot) => void;
  onSelectEnemy: (idx: number) => void;
  onTimedTap: () => void;
}

export const BattleHudOverlay: React.FC<BattleHudOverlayProps> = ({
  party,
  activeSlot,
  potions,
  phase,
  selectedCommand,
  timedRingActive,
  timedRingKind,
  enemies,
  selectedEnemy,
  onCommand,
  onSwapFamiliar,
  onSelectEnemy,
  onTimedTap,
}) => {
  const active = party[activeSlot];
  const isCommandReady = phase === 'COMMAND';

  const commands = [
    {
      label: active.basicName,
      sub: `${active.basicKind} · Timed x2`,
      icon: Sword,
      disabled: false,
    },
    {
      label: active.skillName,
      sub: `${active.skillCost} MP · ${active.skillKind}`,
      icon: active.skillKind === 'SUN' ? Flame : Sparkles,
      disabled: active.mp < active.skillCost,
    },
    {
      label: 'Guard Focus',
      sub: 'Reduce DMG · +6 MP',
      icon: Shield,
      disabled: false,
    },
    {
      label: `Star Tonic (${potions})`,
      sub: 'Party +45 HP / +10 MP',
      icon: Zap,
      disabled: potions <= 0,
    },
  ];

  return (
    <div className="w-full shrink-0 border-t-2 border-[#7C623B] bg-[#141620] px-4 py-2.5 text-[#ECE4D4]">
      <div className="grid grid-cols-1 gap-2.5 lg:grid-cols-12">
        {/* Familiar Hot-Swap Strip (Ni No Kuni L/R instant swap!) */}
        <div className="lg:col-span-5">
          <div className="mb-1 flex items-center justify-between font-pixel text-[10px] text-[#A89E8A]">
            <span>PARTY &amp; FAMILIARS</span>
            <span className="text-[#8AC4E2]">[Q/E] OR CLICK SWAP</span>
          </div>
          <div className="grid grid-cols-3 gap-2">
            {party.map((member) => {
              const isActive = member.slot === activeSlot;
              const hpPct = Math.round((member.hp / member.maxHp) * 100);
              const mpPct = Math.round((member.mp / member.maxMp) * 100);
              return (
                <button
                  key={member.id}
                  type="button"
                  disabled={!isCommandReady || member.hp <= 0}
                  onClick={() => onSwapFamiliar(member.slot)}
                  className={`flex flex-col px-2 py-1.5 text-left font-pixel transition-colors ${
                    isActive
                      ? 'rpg-slot-active'
                      : 'rpg-slot hover:border-[#8D7144]'
                  } disabled:opacity-60`}
                >
                  <div className="flex items-baseline justify-between gap-1">
                    <span className="truncate text-[11px] font-bold text-[#F6EFDF]">
                      {isActive ? '>' : ''}
                      {member.name}
                    </span>
                    <span className="text-[9px] text-[#E6B85C]">
                      {isActive ? 'OUT' : member.sign}
                    </span>
                  </div>
                  <div className="mt-1 space-y-1 text-[9px] tabular-nums">
                    <div className="flex justify-between text-[#C8D4B8]">
                      <span>HP</span>
                      <span>
                        {member.hp}/{member.maxHp}
                      </span>
                    </div>
                    <div className="h-1.5 w-full bg-[#0C0E14] p-[1px]">
                      <div
                        className="h-full bg-[#68B66B] transition-all"
                        style={{ width: `${hpPct}%` }}
                      />
                    </div>
                    <div className="flex justify-between text-[#98C8E6]">
                      <span>MP</span>
                      <span>
                        {member.mp}/{member.maxMp}
                      </span>
                    </div>
                    <div className="h-1.5 w-full bg-[#0C0E14] p-[1px]">
                      <div
                        className="h-full bg-[#52A8DC] transition-all"
                        style={{ width: `${mpPct}%` }}
                      />
                    </div>
                  </div>
                </button>
              );
            })}
          </div>
        </div>

        {/* 4 Tactile Battle Commands (overlays Timed Strike/Parry prompt in-place with zero height shift) */}
        <div className="relative lg:col-span-5">
          <div className="mb-1 flex items-center justify-between font-pixel text-[10px] text-[#A89E8A]">
            <span>
              COMMAND · <span className="text-[#F3C86A]">{active.name}</span>
            </span>
            <span>ARROWS + SPACE</span>
          </div>
          <div className="grid grid-cols-2 gap-1.5">
            {commands.map((cmd, idx) => {
              const Icon = cmd.icon;
              const isSelected = isCommandReady && selectedCommand === idx;
              return (
                <button
                  key={cmd.label}
                  type="button"
                  disabled={!isCommandReady || cmd.disabled}
                  onClick={() => onCommand(idx)}
                  className={`flex items-center gap-2 px-2.5 py-2 text-left font-pixel transition-colors ${
                    isSelected
                      ? 'rpg-slot-active text-[#FFF6DC]'
                      : 'rpg-slot text-[#D6CEBC] hover:border-[#8D7144]'
                  } disabled:cursor-not-allowed disabled:opacity-45`}
                >
                  <Icon
                    className={`h-3.5 w-3.5 shrink-0 ${
                      isSelected ? 'text-[#F3C86A]' : 'text-[#8C8474]'
                    }`}
                  />
                  <div className="min-w-0">
                    <div className="truncate text-[11px] font-bold">{cmd.label}</div>
                    <div className="truncate text-[9px] text-[#9E9584]">{cmd.sub}</div>
                  </div>
                </button>
              );
            })}
          </div>

          {timedRingActive && (
            <div
              className={`rpg-window absolute inset-0 z-10 flex items-center justify-between gap-3 px-3.5 py-2 font-pixel ${
                timedRingKind === 'OFFENSE'
                  ? '!border-[#E6B85C]'
                  : '!border-[#6CC8E8]'
              }`}
            >
              <div className="min-w-0">
                <div
                  className={`text-[11px] font-bold uppercase tracking-wider ${
                    timedRingKind === 'OFFENSE' ? 'text-[#F5C864]' : 'text-[#6CC8E8]'
                  }`}
                >
                  {timedRingKind === 'OFFENSE'
                    ? 'TIMED STRIKE WINDOW!'
                    : 'INCOMING ATTACK — PARRY!'}
                </div>
                <p className="mt-1 text-[10px] leading-snug text-[#F6EBD0]">
                  {timedRingKind === 'OFFENSE'
                    ? 'TAP [SPACE] OR CLICK AS THE RING CLOSES!'
                    : 'TAP [SPACE] OR CLICK ON IMPACT (-55% DMG)!'}
                </p>
              </div>
              <button
                type="button"
                onClick={onTimedTap}
                className={`shrink-0 border-2 px-3 py-1.5 text-[11px] font-bold text-[#14121A] whitespace-nowrap transition-colors ${
                  timedRingKind === 'OFFENSE'
                    ? 'border-[#FFF0B8] bg-[#DDA03A] hover:bg-[#F0B64E]'
                    : 'border-[#D4F6FF] bg-[#5CC8EA] hover:bg-[#7CE0FC]'
                }`}
              >
                {timedRingKind === 'OFFENSE' ? 'STRIKE!' : 'PARRY!'}
              </button>
            </div>
          )}
        </div>

        {/* Target Enemy & Spell Lock Summary */}
        <div className="lg:col-span-2">
          <div className="mb-1 font-pixel text-[10px] text-[#A89E8A]">TARGETS &amp; LOCKS</div>
          <div className="space-y-1.5">
            {enemies.map((e, idx) => {
              const isTarget = e.alive && idx === selectedEnemy;
              return (
                <button
                  key={e.id}
                  type="button"
                  disabled={!isCommandReady || !e.alive}
                  onClick={() => onSelectEnemy(idx)}
                  className={`w-full px-2 py-1.5 text-left font-pixel transition-colors ${
                    !e.alive
                      ? 'rpg-slot opacity-40'
                      : isTarget
                        ? 'rpg-slot-active !border-[#D95C5C]'
                        : 'rpg-slot hover:border-[#8D7144]'
                  }`}
                >
                  <div className="flex items-center justify-between text-[10px] font-bold">
                    <span className="truncate text-[#F6EFDF]">{e.name}</span>
                    <span className="tabular-nums text-[#E88A8A]">
                      {e.hp}/{e.maxHp}
                    </span>
                  </div>
                  <div className="mt-0.5 text-[9px] text-[#A89E8A]">
                    {!e.alive
                      ? 'DEFEATED'
                      : e.staggered
                        ? 'STAGGERED!'
                        : e.locks.length > 0
                          ? e.locks
                              .map((lk, li) => (e.brokenLocks[li] ? '-' : lk))
                              .join(' ')
                          : 'READY'}
                  </div>
                </button>
              );
            })}
          </div>
        </div>
      </div>
    </div>
  );
};
