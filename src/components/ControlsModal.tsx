import React from 'react';
import { Gamepad2, X } from 'lucide-react';

interface ControlsModalProps {
  open: boolean;
  onClose: () => void;
}

const KEY_ROWS = [
  { action: 'Move (8-Way + Corner Slip)', keys: 'WASD / Arrow Keys', pad: 'D-Pad / Left Stick' },
  { action: 'Confirm / Interact / Timed Hit / Parry', keys: 'Space / Enter / Z / Click', pad: 'A / Cross' },
  { action: 'Dash (Overworld) / Cancel', keys: 'Hold Shift / X', pad: 'B / Circle' },
  { action: 'Hot-Swap Familiar (Free Turn)', keys: 'Q / E  or  [ / ]', pad: 'L / R Bumpers' },
  { action: 'Toggle World Atlas Minimap', keys: 'M / Backspace', pad: 'Select' },
  { action: 'Toggle Engine Workbench', keys: 'Tab', pad: '—' },
];

export const ControlsModal: React.FC<ControlsModalProps> = ({ open, onClose }) => {
  if (!open) return null;

  return (
    <div
      className="fixed inset-0 z-50 flex items-center justify-center bg-black/70 p-4 backdrop-blur-xs"
      onClick={onClose}
    >
      <div
        className="w-full max-w-xl rounded-lg border border-[#2F364A] bg-[#171B26] p-6 text-[#ECE7DC] shadow-2xl"
        onClick={(e) => e.stopPropagation()}
      >
        <div className="flex items-start justify-between gap-4 border-b border-[#272D3E] pb-4">
          <div className="flex items-center gap-2.5">
            <Gamepad2 className="h-5 w-5 text-[#F5B841]" />
            <div>
              <h2 className="font-display text-lg font-semibold text-[#F8F3E8]">
                12-Button Retro Input &amp; Mechanics Guide
              </h2>
              <p className="text-xs text-[#9AA3B8]">
                Keyboard, Mouse, and USB/Bluetooth Controllers work seamlessly out of the box.
              </p>
            </div>
          </div>
          <button
            type="button"
            onClick={onClose}
            className="rounded p-1.5 text-[#9AA3B8] hover:bg-[#23293A] hover:text-white"
            aria-label="Close Controls"
          >
            <X className="h-4 w-4" />
          </button>
        </div>

        <div className="mt-4 overflow-hidden rounded border border-[#262C3C]">
          <table className="w-full text-left text-xs">
            <thead className="bg-[#12151E] text-[#9AA3B8]">
              <tr>
                <th className="px-3 py-2 font-medium">Action</th>
                <th className="px-3 py-2 font-medium">Keyboard / Mouse</th>
                <th className="px-3 py-2 font-medium">Gamepad</th>
              </tr>
            </thead>
            <tbody className="divide-y divide-[#232838]">
              {KEY_ROWS.map((row) => (
                <tr key={row.action} className="bg-[#181C28]">
                  <td className="px-3 py-2.5 font-medium text-[#F4EFE4]">{row.action}</td>
                  <td className="px-3 py-2.5 font-mono text-[11px] text-[#F5B841]">{row.keys}</td>
                  <td className="px-3 py-2.5 font-mono text-[11px] text-[#9CC7E8]">{row.pad}</td>
                </tr>
              ))}
            </tbody>
          </table>
        </div>

        <div className="mt-4 space-y-2 text-xs leading-relaxed text-[#B8C0D2]">
          <p>
            <strong className="text-[#F6F1E6]">Sea of Stars Timed Combat:</strong> Tap{' '}
            <span className="font-mono text-[#F5B841]">Space</span> or click the canvas right as
            the contracting ring hits your target to trigger crunchy hitstop, a follow-up strike,
            +4 MP, and extra Spell Lock breaks. Tap on incoming strikes for a Timed Guard.
          </p>
          <p>
            <strong className="text-[#F6F1E6]">Ni No Kuni Familiar Swapping:</strong> Press{' '}
            <span className="font-mono text-[#5AD2F4]">Q / E</span> (or click a party card) to
            hot-swap between Rowan, Bramblekit (Sword / Sun), and Lumina (Star) without spending
            your turn—ideal for shattering multi-element Boss Locks.
          </p>
        </div>

        <div className="mt-5 flex justify-end">
          <button
            type="button"
            onClick={onClose}
            className="rounded bg-[#D4942A] px-4 py-2 text-xs font-semibold text-[#151924] hover:bg-[#E7AA40]"
          >
            Back to Engine
          </button>
        </div>
      </div>
    </div>
  );
};
