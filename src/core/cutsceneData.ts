/**
 * Project Hearth — Compact Bytecode Cutscene VM Scripts
 *
 * Designed as flat, static ROM-style command arrays so cutscenes translate
 * directly into C99 `const CutsceneOp script[]` tables.
 */

import { Direction, DIR_DOWN, DIR_LEFT, DIR_RIGHT, DIR_UP, PortraitId, SfxId } from './types';

export type CutsceneOpcode =
  | 'WAIT'
  | 'PAN_CAM'
  | 'CAM_FOLLOW'
  | 'WALK_ACTOR'
  | 'FACE_ACTOR'
  | 'EMOTE'
  | 'SAY'
  | 'SFX'
  | 'SHAKE'
  | 'HEART_ORB'
  | 'SET_FLAG'
  | 'END';

export type ActorTargetId = 'hero' | 'pip' | 'elder' | 'clara' | 'guard';

export interface CutsceneOp {
  op: CutsceneOpcode;
  frames?: number;
  target?: ActorTargetId;
  toTarget?: ActorTargetId;
  x?: number; // tile coords for PAN_CAM or WALK_ACTOR
  y?: number;
  dir?: Direction;
  emote?: 'bang' | 'heart' | 'question' | 'spark';
  speaker?: string;
  portrait?: PortraitId;
  text?: string;
  voice?: SfxId;
  sfx?: SfxId;
  intensity?: number;
  flag?: 'hasCourage' | 'guardMended' | 'elderCutsceneSeen';
  orbColor?: string;
}

export const CUTSCENE_INTRO_DEMO: CutsceneOp[] = [
  { op: 'PAN_CAM', x: 7, y: 8, frames: 42 },
  { op: 'WAIT', frames: 18 },
  { op: 'SFX', sfx: SfxId.EMOTE },
  { op: 'EMOTE', target: 'pip', emote: 'spark', frames: 34 },
  { op: 'CAM_FOLLOW', frames: 30 },
  { op: 'FACE_ACTOR', target: 'hero', dir: DIR_LEFT },
  { op: 'FACE_ACTOR', target: 'elder', dir: DIR_DOWN },
  {
    op: 'SAY',
    speaker: 'Pip',
    portrait: 'pip',
    voice: SfxId.TEXT_PIP,
    text: "Oi! Wake up, Rowan! Windmill's turning, meadow grass is humming, and this whole 16-bit engine is purring at sixty frames a second!",
  },
  { op: 'EMOTE', target: 'hero', emote: 'question', frames: 26 },
  {
    op: 'SAY',
    speaker: 'Rowan',
    portrait: 'hero',
    voice: SfxId.TEXT_HERO,
    text: "Smooth footsteps, rustling grass... even my brass locket feels warm. Where should we start testing?",
  },
  { op: 'PAN_CAM', x: 21, y: 13, frames: 38 },
  { op: 'WAIT', frames: 16 },
  {
    op: 'SAY',
    speaker: 'Pip',
    portrait: 'pip',
    voice: SfxId.TEXT_PIP,
    text: "Guard Bram by the eastern bridge is frozen stiff with a Hollow heart—borrow a spark of Courage from Clara here in the square, or sprint across the creek and test our Familiars in battle!",
  },
  { op: 'CAM_FOLLOW', frames: 26 },
  { op: 'FACE_ACTOR', target: 'hero', dir: DIR_DOWN },
  { op: 'SET_FLAG', flag: 'elderCutsceneSeen' },
  { op: 'END' },
];

export const CUTSCENE_ELDER_TALK: CutsceneOp[] = [
  { op: 'FACE_ACTOR', target: 'elder', dir: DIR_DOWN },
  { op: 'SFX', sfx: SfxId.EMOTE },
  { op: 'EMOTE', target: 'elder', emote: 'spark', frames: 24 },
  {
    op: 'SAY',
    speaker: 'Elder Wren',
    portrait: 'elder',
    voice: SfxId.TEXT_ELDER,
    text: "Ah, Rowan! You move lightly today—no stiff grid-stepping for you. Have you tried holding Dash through the wildgrass or splashing across the southern ford?",
  },
  { op: 'PAN_CAM', x: 37, y: 13, frames: 45 },
  { op: 'WAIT', frames: 20 },
  {
    op: 'SAY',
    speaker: 'Elder Wren',
    portrait: 'elder',
    voice: SfxId.TEXT_ELDER,
    text: "Out past the timber bridge sits the old Stone Shrine. The Briar Warden guards it with Spell Locks—hot-swap between Bramblekit and Lumina with L and R to shatter every lock!",
  },
  { op: 'CAM_FOLLOW', frames: 34 },
  { op: 'SET_FLAG', flag: 'elderCutsceneSeen' },
  { op: 'END' },
];

export const CUTSCENE_TAKE_COURAGE: CutsceneOp[] = [
  { op: 'FACE_ACTOR', target: 'hero', dir: DIR_UP },
  { op: 'FACE_ACTOR', target: 'clara', dir: DIR_DOWN },
  {
    op: 'SAY',
    speaker: 'Clara',
    portrait: 'clara',
    voice: SfxId.TEXT_PIP,
    text: "Climbing the windmill rafters to rescue baby swallows? Easy! Take a spark of my Courage in your locket—I've got plenty to spare!",
  },
  { op: 'SFX', sfx: SfxId.HEART_TAKE },
  { op: 'HEART_ORB', target: 'clara', toTarget: 'hero', frames: 42, orbColor: '#F5B841' },
  { op: 'SHAKE', intensity: 2, frames: 10 },
  { op: 'EMOTE', target: 'hero', emote: 'heart', frames: 32 },
  { op: 'SET_FLAG', flag: 'hasCourage' },
  {
    op: 'SAY',
    speaker: 'Pip',
    portrait: 'pip',
    voice: SfxId.TEXT_PIP,
    text: "Got it! A bright spark of Courage is tucked safely inside your Heart Locket. Take it east to Guard Bram by the timber bridge!",
  },
  { op: 'END' },
];

export const CUTSCENE_GIVE_COURAGE: CutsceneOp[] = [
  { op: 'FACE_ACTOR', target: 'guard', dir: DIR_LEFT },
  { op: 'FACE_ACTOR', target: 'hero', dir: DIR_RIGHT },
  {
    op: 'SAY',
    speaker: 'Rowan',
    portrait: 'hero',
    voice: SfxId.TEXT_HERO,
    text: "Hold steady, Bram! I brought a spark of Courage from the hamlet—let's mend that hollow chill together!",
  },
  { op: 'SFX', sfx: SfxId.HEART_GIVE },
  { op: 'HEART_ORB', target: 'hero', toTarget: 'guard', frames: 46, orbColor: '#F5B841' },
  { op: 'SHAKE', intensity: 3, frames: 14 },
  { op: 'SET_FLAG', flag: 'guardMended' },
  { op: 'EMOTE', target: 'guard', emote: 'heart', frames: 38 },
  {
    op: 'SAY',
    speaker: 'Guard Bram',
    portrait: 'guard',
    voice: SfxId.TEXT_ELDER,
    text: "Whoa... my hands stopped shaking! It feels like sunlight broke through fog. Thank you, lad—take this Star Tonic for the Shrine ahead!",
  },
  { op: 'END' },
];
