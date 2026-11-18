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
    <div className="w-full shrink-0 border-t border-[#282E3F] bg-[#151924] px-4 py-3 text-[#ECE7DC]">
      <div className="grid grid-cols-1 gap-3 lg:grid-cols-12">
        {/* Familiar Hot-Swap Strip (Ni No Kuni L/R instant swap!) */}
        <div className="lg:col-span-5">
          <div className="mb-1.5 flex items-center justify-between text-[11px] text-[#9AA3B8]">
            <span>Active Party &amp; Familiars</span>
            <span className="font-mono">Q / E or Click to Swap (Free)</span>
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
                  className={`flex flex-col rounded border p-2 text-left transition-colors ${
                    isActive
                      ? 'border-[#D4942A] bg-[#21273A]'
                      : 'border-[#272E40] bg-[#191E2C] hover:border-[#495572]'
                  } disabled:opacity-60`}
                >
                  <div className="flex items-baseline justify-between gap-1">
                    <span className="truncate text-xs font-semibold text-[#F6F1E6]">
                      {member.name}
                    </span>
                    <span className="font-mono text-[10px] text-[#D4942A]">
                      {isActive ? 'OUT' : member.sign}
                    </span>
                  </div>
                  <div className="mt-1.5 space-y-1 font-mono text-[10px] tabular-nums">
                    <div className="flex justify-between text-[#C9D2E3]">
                      <span>HP</span>
                      <span>
                        {member.hp}/{member.maxHp}
                      </span>
                    </div>
                    <div className="h-1 w-full overflow-hidden rounded-full bg-[#0E1118]">
                      <div
                        className="h-full bg-[#5DB068] transition-all"
                        style={{ width: `${hpPct}%` }}
                      />
                    </div>
                    <div className="flex justify-between text-[#9CC7E8]">
                      <span>MP</span>
                      <span>
                        {member.mp}/{member.maxMp}
                      </span>
                    </div>
                    <div className="h-1 w-full overflow-hidden rounded-full bg-[#0E1118]">
                      <div
                        className="h-full bg-[#4EA5D9] transition-all"
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
          <div className="mb-1.5 flex items-center justify-between text-[11px] text-[#9AA3B8]">
            <span>
              Commands · <span className="text-[#F5B841]">{active.name}</span>
            </span>
            <span className="font-mono">Arrows + Space / Click</span>
          </div>
          <div className="grid grid-cols-2 gap-2">
            {commands.map((cmd, idx) => {
              const Icon = cmd.icon;
              const isSelected = isCommandReady && selectedCommand === idx;
              return (
                <button
                  key={cmd.label}
                  type="button"
                  disabled={!isCommandReady || cmd.disabled}
                  onClick={() => onCommand(idx)}
                  className={`flex items-center gap-2.5 rounded border px-3 py-2 text-left transition-colors ${
                    isSelected
                      ? 'border-[#D4942A] bg-[#252C42] text-[#FFF6DE]'
                      : 'border-[#272E40] bg-[#191E2C] text-[#D7DCE8] hover:border-[#4A5570]'
                  } disabled:cursor-not-allowed disabled:opacity-45`}
                >
                  <Icon
                    className={`h-4 w-4 shrink-0 ${
                      isSelected ? 'text-[#F5B841]' : 'text-[#8F98AE]'
                    }`}
                  />
                  <div className="min-w-0">
                    <div className="truncate text-xs font-semibold">{cmd.label}</div>
                    <div className="truncate font-mono text-[10px] text-[#959FB5]">{cmd.sub}</div>
                  </div>
                </button>
              );
            })}
          </div>

          {timedRingActive && (
            <div
              className={`absolute inset-0 z-10 flex items-center justify-between gap-3 rounded-md border-2 px-4 py-3 shadow-lg ${
                timedRingKind === 'OFFENSE'
                  ? 'border-[#D4942A]/90 bg-[#1C2233]/96'
                  : 'border-[#5AD2F4]/90 bg-[#182234]/96'
              }`}
            >
              <div className="min-w-0">
                <div
                  className={`font-mono text-[11px] font-semibold uppercase tracking-wider ${
                    timedRingKind === 'OFFENSE' ? 'text-[#F5B841]' : 'text-[#5AD2F4]'
                  }`}
                >
                  {timedRingKind === 'OFFENSE'
                    ? 'Timed Strike Window'
                    : 'Incoming Attack — Timed Parry'}
                </div>
                <p className="mt-1 text-xs leading-snug text-[#FFF1C4]">
                  {timedRingKind === 'OFFENSE'
                    ? 'Tap SPACE / Z or Click right as the golden ring meets the target!'
                    : 'Tap SPACE / Z or Click on impact (-55% DMG & +3 MP)!'}
                </p>
              </div>
              <button
                type="button"
                onClick={onTimedTap}
                className={`shrink-0 rounded px-3.5 py-2 text-xs font-semibold text-[#151924] whitespace-nowrap transition-colors ${
                  timedRingKind === 'OFFENSE'
                    ? 'bg-[#D4942A] hover:bg-[#E7AA40]'
                    : 'bg-[#5AD2F4] hover:bg-[#7CE0FC]'
                }`}
              >
                {timedRingKind === 'OFFENSE' ? 'Strike Now' : 'Parry Now'}
              </button>
            </div>
          )}
        </div>

        {/* Target Enemy & Spell Lock Summary */}
        <div className="lg:col-span-2">
          <div className="mb-1.5 text-[11px] text-[#9AA3B8]">Targets &amp; Locks</div>
          <div className="space-y-1.5">
            {enemies.map((e, idx) => {
              const isTarget = e.alive && idx === selectedEnemy;
              return (
                <button
                  key={e.id}
                  type="button"
                  disabled={!isCommandReady || !e.alive}
                  onClick={() => onSelectEnemy(idx)}
                  className={`w-full rounded border px-2.5 py-1.5 text-left transition-colors ${
                    !e.alive
                      ? 'border-[#222838] bg-[#141824] opacity-40'
                      : isTarget
                        ? 'border-[#E04E4E] bg-[#261D26]'
                        : 'border-[#272E40] bg-[#191E2C] hover:border-[#4A5570]'
                  }`}
                >
                  <div className="flex items-center justify-between text-xs font-medium">
                    <span className="truncate text-[#F6F1E6]">{e.name}</span>
                    <span className="font-mono text-[10px] tabular-nums text-[#E98A8A]">
                      {e.hp}/{e.maxHp}
                    </span>
                  </div>
                  <div className="mt-0.5 font-mono text-[10px] text-[#9AA3B8]">
                    {!e.alive
                      ? 'DEFEATED'
                      : e.staggered
                        ? 'STAGGERED'
                        : e.locks.length > 0
                          ? 'Locks: ' +
                            e.locks
                              .map((lk, li) => (e.brokenLocks[li] ? '—' : lk))
                              .join(' · ')
                          : 'Ready'}
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
