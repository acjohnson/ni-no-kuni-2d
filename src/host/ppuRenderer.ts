/**
 * Project Hearth — Software Virtual PPU Renderer (320x224 Genesis / 256x224 SNES)
 *
 * Thin replaceable host renderer that consumes a pure PpuFramePacket from
 * HearthEngine and draws crisp nearest-neighbor 16-bit layers onto the canvas.
 */

import { MAP_H, MAP_W, PALETTES, TILE_SIZE, WorldMapData } from '../core/mapData';
import {
  BattleEnemyState,
  FP_ONE,
  LockKind,
  OamSprite,
  PartyMemberState,
  PpuFramePacket,
} from '../core/types';
import { PixelAtlas } from './pixelAtlas';

export class SoftwarePpuRenderer {
  private scratchCanvas: HTMLCanvasElement;
  private scratchCtx: CanvasRenderingContext2D;

  constructor() {
    this.scratchCanvas = document.createElement('canvas');
    this.scratchCanvas.width = 48;
    this.scratchCanvas.height = 48;
    this.scratchCtx = this.scratchCanvas.getContext('2d')!;
  }

  public render(
    canvas: HTMLCanvasElement,
    packet: PpuFramePacket,
    map: WorldMapData,
    atlas: PixelAtlas,
    party: PartyMemberState[]
  ): void {
    if (canvas.width !== packet.width || canvas.height !== packet.height) {
      canvas.width = packet.width;
      canvas.height = packet.height;
    }

    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.imageSmoothingEnabled = false;

    ctx.save();
    ctx.clearRect(0, 0, packet.width, packet.height);
    ctx.translate(packet.shakeX, packet.shakeY);

    if (
      packet.mode === 'BATTLE' ||
      packet.mode === 'BATTLE_VICTORY' ||
      packet.mode === 'BATTLE_DEFEAT'
    ) {
      this.renderBattleScene(ctx, packet, atlas, party);
    } else {
      this.renderOverworldScene(ctx, packet, map, atlas);
    }

    // White impact flash register (crunchy hitstop/crit feedback)
    if (packet.flashAlpha > 0.01) {
      ctx.fillStyle = `rgba(255, 250, 232, ${Math.min(0.65, packet.flashAlpha)})`;
      ctx.fillRect(-8, -8, packet.width + 16, packet.height + 16);
    }

    ctx.restore();
  }

  /* ========================================================================
   * OVERWORLD VIRTUAL PPU PIPELINE (BG1 -> OAM -> BG2 -> FX -> PROMPTS)
   * ======================================================================== */
  private renderOverworldScene(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    map: WorldMapData,
    atlas: PixelAtlas
  ): void {
    const { width, height, camX, camY, frameCount } = packet;
    const animTick = frameCount >> 4;

    const startCol = Math.max(0, Math.floor(camX / TILE_SIZE));
    const endCol = Math.min(MAP_W - 1, Math.ceil((camX + width) / TILE_SIZE));
    const startRow = Math.max(0, Math.floor(camY / TILE_SIZE));
    const endRow = Math.min(MAP_H - 1, Math.ceil((camY + height) / TILE_SIZE));

    // 1. BG1 Terrain & Architecture Plane
    for (let r = startRow; r <= endRow; r++) {
      const sy = r * TILE_SIZE - camY;
      for (let c = startCol; c <= endCol; c++) {
        const sx = c * TILE_SIZE - camX;
        const tileId = map.bg1[r * MAP_W + c];
        const img = atlas.getBg1Tile(tileId, animTick);
        ctx.drawImage(img, sx, sy, TILE_SIZE, TILE_SIZE);
      }
    }

    // 2. OAM Layer 0 & Layer 1 (Y-Sorted Actors, Props & Auras)
    for (const spr of packet.sprites) {
      if (spr.layer <= 1) {
        this.drawOamSprite(ctx, spr, atlas, frameCount);
      }
    }

    // 3. BG2 Foreground Overhead Plane (Tree Canopies, Roof Eaves, Shrine Arch!)
    for (let r = startRow; r <= endRow; r++) {
      const sy = r * TILE_SIZE - camY;
      for (let c = startCol; c <= endCol; c++) {
        const bg2Id = map.bg2[r * MAP_W + c];
        if (bg2Id === 0) continue;
        const tileImg = atlas.getBg2Tile(bg2Id);
        if (tileImg) {
          ctx.drawImage(tileImg, c * TILE_SIZE - camX, sy, TILE_SIZE, TILE_SIZE);
        }
      }
    }

    // 4. OAM Layer 2 (Windmill Blades, Emotes, Heart-Orb Transfer!)
    for (const spr of packet.sprites) {
      if (spr.layer === 2) {
        this.drawOamSprite(ctx, spr, atlas, frameCount);
      }
    }

    // 5. World-Space Particles
    this.renderParticles(ctx, packet, false);

    // 6. Floating World Pop-up Text
    this.renderFloatingTexts(ctx, packet, true);

    // 7. Debug Hitboxes (if toggled in Feel Tuner)
    for (const b of packet.debugBoxes) {
      ctx.strokeStyle = b.color;
      ctx.lineWidth = 1;
      ctx.strokeRect(b.x + 0.5, b.y + 0.5, b.w, b.h);
    }

    // 8. Contextual Crisp Pixel Interact Bubble
    if (packet.interactPrompt.active) {
      const bob = Math.round(Math.sin(frameCount * 0.14) * 1.5);
      const text = `[A] ${packet.interactPrompt.label}`;
      ctx.font = 'bold 8px monospace';
      const tw = Math.ceil(ctx.measureText(text).width) + 8;
      const bx = Math.max(4, Math.min(width - tw - 4, Math.round(packet.interactPrompt.x - tw / 2)));
      const by = Math.max(4, Math.round(packet.interactPrompt.y - 12 + bob));

      ctx.fillStyle = '#181E2E';
      ctx.fillRect(bx, by, tw, 12);
      ctx.strokeStyle = '#F5B841';
      ctx.lineWidth = 1;
      ctx.strokeRect(bx + 0.5, by + 0.5, tw - 1, 11);
      ctx.fillStyle = '#FFF6CC';
      ctx.textBaseline = 'top';
      ctx.fillText(text, bx + 4, by + 2);
    }
  }

  /* ========================================================================
   * IN-PLACE SEAMLESS 16-BIT BATTLE ARENA PIPELINE
   * ======================================================================== */
  private renderBattleScene(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    atlas: PixelAtlas,
    party: PartyMemberState[]
  ): void {
    const { width, height, frameCount, battle } = packet;
    const isShrine = battle.biome === 'SHRINE';

    // 1. Layered 16-bit Sky & Horizon Backdrop (cohesive with world biome!)
    const skyBands = isShrine
      ? ['#1D2236', '#28314C', '#384666', '#4E5D7B']
      : ['#5AA0D8', '#7BBCE8', '#A6D7F2', '#DCEFC8'];
    const bandH = 19;
    skyBands.forEach((col, i) => {
      ctx.fillStyle = col;
      ctx.fillRect(0, i * bandH, width, bandH + 1);
    });

    // Drifting pixel clouds
    ctx.fillStyle = isShrine ? '#495472' : '#F4F7EE';
    for (let i = 0; i < 4; i++) {
      const cx = ((i * 96 + Math.floor(frameCount * 0.15)) % (width + 64)) - 32;
      const cy = 14 + (i % 2) * 14;
      ctx.fillRect(cx, cy, 34, 7);
      ctx.fillRect(cx + 6, cy - 4, 20, 5);
    }

    // Distant rolling hills silhouette + windmill / shrine monoliths
    ctx.fillStyle = isShrine ? '#2A3548' : '#4A884E';
    for (let x = 0; x < width; x += 4) {
      const h = Math.round(14 + Math.sin(x * 0.03) * 6 + Math.cos(x * 0.015) * 5);
      ctx.fillRect(x, 76 - h, 4, h + 12);
    }

    if (isShrine) {
      // Distant Shrine stone pillars
      ctx.fillStyle = '#3E4656';
      ctx.fillRect(40, 34, 14, 48);
      ctx.fillRect(width - 62, 30, 16, 52);
      ctx.fillStyle = '#748094';
      ctx.fillRect(42, 36, 4, 44);
      ctx.fillRect(width - 60, 32, 5, 48);
    } else {
      // Distant Windmill silhouette on the horizon!
      ctx.fillStyle = '#6A7586';
      ctx.fillRect(width - 78, 36, 16, 36);
      ctx.fillStyle = '#3B5998';
      ctx.fillRect(width - 80, 30, 20, 8);
      // Rotating mini sails
      const sails = atlas.getSprite('windmill_blades', 0, 0);
      ctx.save();
      ctx.translate(width - 70, 36);
      ctx.rotate(((frameCount * 0.8) * Math.PI) / 180);
      ctx.drawImage(sails, -12, -12, 24, 24);
      ctx.restore();
    }

    // 2. Ground Battle Stage Plane
    const groundY = 76;
    ctx.fillStyle = isShrine ? '#3D4555' : '#5EA055';
    ctx.fillRect(0, groundY, width, height - groundY);

    // Textured perspective strips & Shrine Rune Circle or Meadow Grass Tufts
    if (isShrine) {
      ctx.fillStyle = '#535E72';
      ctx.fillRect(0, groundY + 14, width, 74);
      // Glowing carved stone rune circle
      ctx.strokeStyle = (frameCount >> 4) & 1 ? '#5AD2F4' : '#A6D7F2';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.ellipse(width * 0.5, 128, 76, 26, 0, 0, Math.PI * 2);
      ctx.stroke();
      ctx.fillStyle = '#2B3140';
      ctx.fillRect(0, 168, width, height - 168);
    } else {
      ctx.fillStyle = '#74B461';
      ctx.fillRect(0, groundY + 4, width, 16);
      // Sunlit path band where combatants duel
      ctx.fillStyle = '#C89B62';
      ctx.fillRect(0, 102, width, 54);
      ctx.fillStyle = '#DFBA84';
      ctx.fillRect(0, 106, width, 4);
      ctx.fillRect(0, 148, width, 3);
      ctx.fillStyle = '#3B7542';
      ctx.fillRect(0, 162, width, height - 162);
    }

    // 3. Draw Rowan in Rear Support Stance when a Familiar is active!
    if (battle.activeSlot !== 0) {
      const rowanImg = atlas.getSprite('hero', 3, (frameCount >> 5) & 1);
      this.drawShadow(ctx, battle.wizardBackX, battle.wizardBackY, 14);
      ctx.drawImage(
        rowanImg,
        Math.round(battle.wizardBackX - rowanImg.width / 2),
        Math.round(battle.wizardBackY - rowanImg.height + 2)
      );
      // Pip hovering faithfully behind Rowan!
      const pipImg = atlas.getSprite('pip', 3, (frameCount >> 3) & 1);
      const pipBob = Math.round(Math.sin(frameCount * 0.14) * 2);
      ctx.drawImage(
        pipImg,
        Math.round(battle.wizardBackX - 18),
        Math.round(battle.wizardBackY - 26 + pipBob)
      );
    }

    // 4. Soul-Arc Light Ribbon during Ni No Kuni Familiar Hot-Swap!
    if (battle.phase === 'SWAP_ARC') {
      const t = battle.swapArcProgress;
      ctx.save();
      ctx.strokeStyle = '#F5B841';
      ctx.lineWidth = 2;
      ctx.beginPath();
      ctx.moveTo(battle.wizardBackX + 4, battle.wizardBackY - 12);
      const midX = (battle.wizardBackX + battle.heroX) * 0.5;
      const midY = Math.min(battle.wizardBackY, battle.heroY) - 42;
      ctx.quadraticCurveTo(midX, midY, battle.heroX, battle.heroY - 12);
      ctx.stroke();

      // Luminous traveling soul-orb along the quadratic curve
      const inv = 1 - t;
      const orbX = inv * inv * (battle.wizardBackX + 4) + 2 * inv * t * midX + t * t * battle.heroX;
      const orbY = inv * inv * (battle.wizardBackY - 12) + 2 * inv * t * midY + t * t * (battle.heroY - 12);
      ctx.fillStyle = '#FFF6CC';
      ctx.fillRect(Math.round(orbX - 4), Math.round(orbY - 4), 8, 8);
      ctx.fillStyle = '#5AD2F4';
      ctx.fillRect(Math.round(orbX - 2), Math.round(orbY - 2), 4, 4);
      ctx.restore();
    }

    // 5. Active Frontline Party Combatant (Hero or Familiar)
    const activeMember = party[battle.activeSlot];
    if (battle.phase !== 'SWAP_ARC' || battle.swapArcProgress > 0.72) {
      const sprImg = atlas.getSprite(
        activeMember.spriteId,
        activeMember.slot === 0 ? 3 : 0,
        (frameCount >> 4) & 1
      );
      this.drawShadow(ctx, battle.heroX, battle.heroY, 18);

      // Guarding aura shield ring
      if (battle.heroGuarding) {
        ctx.strokeStyle = '#5AD2F4';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.ellipse(battle.heroX, battle.heroY - 10, 14, 15, 0, 0, Math.PI * 2);
        ctx.stroke();
      }

      this.drawScaledSprite(
        ctx,
        sprImg,
        battle.heroX,
        battle.heroY,
        battle.heroScaleXFp,
        battle.heroScaleYFp,
        false,
        battle.heroFlashFrames > 0
      );
    }

    // 6. Living Enemies + Target Reticle + Spell Locks
    battle.enemies.forEach((enemy, idx) => {
      if (!enemy.alive) return;
      this.drawBattleEnemy(
        ctx,
        enemy,
        idx === battle.selectedEnemy && battle.phase === 'COMMAND',
        atlas,
        frameCount
      );
    });

    // 7. Battle Screen-Space Particles
    this.renderParticles(ctx, packet, true);

    // 8. Diegetic Sea of Stars-Style Timed Hit / Parry Convergence Ring!
    if (battle.timedRing.active) {
      const tr = battle.timedRing;
      const isParry = tr.kind === 'PARRY';
      const inSweet = tr.progress >= tr.sweetStart && tr.progress <= tr.sweetEnd;
      const baseR = 8;
      const shrinkR = Math.max(baseR - 2, Math.round(baseR + (1 - Math.min(1.05, tr.progress)) * 22));

      ctx.save();
      // Target inner anchor circle
      ctx.strokeStyle = isParry ? 'rgba(90, 210, 244, 0.8)' : 'rgba(245, 184, 65, 0.8)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.arc(tr.x, tr.y, baseR, 0, Math.PI * 2);
      ctx.stroke();

      // Contracting timing ring (blazes bright inside sweet spot!)
      ctx.strokeStyle =
        tr.result === 'PERFECT'
          ? '#FFFFFF'
          : inSweet
            ? isParry
              ? '#5AD2F4'
              : '#FFF1A8'
            : isParry
              ? '#3B82B8'
              : '#D4942A';
      ctx.lineWidth = inSweet ? 2.5 : 1.5;
      ctx.beginPath();
      ctx.arc(tr.x, tr.y, shrinkR, 0, Math.PI * 2);
      ctx.stroke();

      // Micro cue prompt when inside sweet spot
      if (inSweet && tr.result === 'NONE') {
        ctx.font = 'bold 8px monospace';
        const cue = isParry ? 'GUARD!' : 'TAP!';
        const cw = ctx.measureText(cue).width;
        ctx.fillStyle = '#141824';
        ctx.fillRect(Math.round(tr.x - cw / 2 - 3), Math.round(tr.y - 22), Math.ceil(cw + 6), 10);
        ctx.fillStyle = isParry ? '#5AD2F4' : '#FFF1A8';
        ctx.textBaseline = 'top';
        ctx.fillText(cue, Math.round(tr.x - cw / 2), Math.round(tr.y - 21));
      }
      ctx.restore();
    }

    // 9. Floating Damage / Critical / Lock-Break Numbers
    this.renderFloatingTexts(ctx, packet, false);

    // 10. Top Center Crisp Pixel Banner (e.g. "SOLAR SLAM!" / "BRIAR WARDEN!")
    if (battle.bannerFrames > 0 && battle.bannerText) {
      ctx.font = 'bold 8px monospace';
      const bw = Math.ceil(ctx.measureText(battle.bannerText).width) + 16;
      const bx = Math.round((width - bw) / 2);
      ctx.fillStyle = 'rgba(20, 24, 38, 0.92)';
      ctx.fillRect(bx, 6, bw, 14);
      ctx.strokeStyle = '#F5B841';
      ctx.lineWidth = 1;
      ctx.strokeRect(bx + 0.5, 6.5, bw - 1, 13);
      ctx.fillStyle = '#FFF6CC';
      ctx.textBaseline = 'top';
      ctx.fillText(battle.bannerText, bx + 8, 9);
    }
  }

  private drawBattleEnemy(
    ctx: CanvasRenderingContext2D,
    enemy: BattleEnemyState,
    isTargeted: boolean,
    atlas: PixelAtlas,
    frameCount: number
  ): void {
    const sprImg = atlas.getSprite(enemy.spriteId, 0, (enemy.animFrame >> 4) & 1);
    const shadowW = enemy.isBoss ? 30 : 18;
    this.drawShadow(ctx, enemy.x, enemy.y, shadowW);

    const recoilX = enemy.hurtFrames > 0 ? ((enemy.hurtFrames & 2) ? 2 : -2) : 0;
    this.drawScaledSprite(
      ctx,
      sprImg,
      enemy.x + recoilX,
      enemy.y,
      enemy.scaleXFp,
      enemy.scaleYFp,
      false,
      enemy.flashFrames > 0
    );

    const topY = enemy.y - sprImg.height - 4;

    // Compact enemy HP bar
    const barW = enemy.isBoss ? 36 : 24;
    const barX = Math.round(enemy.x - barW / 2);
    const hpRatio = Math.max(0, enemy.hp / enemy.maxHp);
    ctx.fillStyle = '#181C28';
    ctx.fillRect(barX - 1, enemy.y + 4, barW + 2, 5);
    ctx.fillStyle = enemy.staggered ? '#F5B841' : '#E04E4E';
    ctx.fillRect(barX, enemy.y + 5, Math.round(barW * hpRatio), 3);

    // Crisp Target Selector Pointer
    if (isTargeted) {
      const bob = Math.round(Math.sin(frameCount * 0.2) * 2);
      const py = topY - (enemy.locks.length > 0 ? 15 : 4) + bob;
      ctx.fillStyle = '#181C28';
      ctx.fillRect(enemy.x - 4, py - 4, 8, 6);
      ctx.fillStyle = '#F5B841';
      ctx.fillRect(enemy.x - 3, py - 3, 6, 2);
      ctx.fillRect(enemy.x - 2, py - 1, 4, 2);
      ctx.fillRect(enemy.x - 1, py + 1, 2, 1);
    }

    // Sea of Stars-Style Spell Lock Icons above charging enemies!
    if (enemy.chargeTurns > 0 && enemy.locks.length > 0) {
      const lockSpacing = 12;
      const totalW = enemy.locks.length * lockSpacing;
      const startX = Math.round(enemy.x - totalW / 2 + 1);
      const ly = topY - 10;

      // Countdown turns pip
      ctx.fillStyle = '#191421';
      ctx.fillRect(startX - 11, ly, 10, 10);
      ctx.fillStyle = '#E04E4E';
      ctx.font = 'bold 8px monospace';
      ctx.textBaseline = 'top';
      ctx.fillText(String(enemy.chargeTurns), startX - 8, ly + 1);

      enemy.locks.forEach((lk, i) => {
        const lx = startX + i * lockSpacing;
        const broken = enemy.brokenLocks[i];
        this.drawLockIcon(ctx, lx, ly, lk, broken);
      });
    }
  }

  private drawLockIcon(
    ctx: CanvasRenderingContext2D,
    x: number,
    y: number,
    kind: LockKind,
    broken: boolean
  ): void {
    ctx.fillStyle = broken ? 'rgba(28, 32, 46, 0.5)' : '#1C2234';
    ctx.fillRect(x, y, 10, 10);
    ctx.strokeStyle = broken
      ? '#475066'
      : kind === 'SWORD'
        ? '#E8DEC8'
        : kind === 'SUN'
          ? '#F5B841'
          : '#5AD2F4';
    ctx.lineWidth = 1;
    ctx.strokeRect(x + 0.5, y + 0.5, 9, 9);

    if (broken) {
      ctx.fillStyle = '#525C73';
      ctx.fillRect(x + 2, y + 4, 6, 1);
      return;
    }

    if (kind === 'SWORD') {
      ctx.fillStyle = '#EAE0C8';
      ctx.fillRect(x + 6, y + 2, 2, 2);
      ctx.fillRect(x + 5, y + 3, 2, 2);
      ctx.fillRect(x + 4, y + 4, 2, 2);
      ctx.fillStyle = '#E04E4E';
      ctx.fillRect(x + 2, y + 6, 3, 2);
    } else if (kind === 'SUN') {
      ctx.fillStyle = '#F5B841';
      ctx.fillRect(x + 3, y + 3, 4, 4);
      ctx.fillStyle = '#FFF6CC';
      ctx.fillRect(x + 4, y + 4, 2, 2);
      ctx.fillRect(x + 4, y + 1, 2, 1);
      ctx.fillRect(x + 4, y + 8, 2, 1);
      ctx.fillRect(x + 1, y + 4, 1, 2);
      ctx.fillRect(x + 8, y + 4, 1, 2);
    } else {
      ctx.fillStyle = '#5AD2F4';
      ctx.fillRect(x + 4, y + 2, 2, 6);
      ctx.fillRect(x + 2, y + 4, 6, 2);
      ctx.fillStyle = '#FFFFFF';
      ctx.fillRect(x + 4, y + 4, 2, 2);
    }
  }

  /* ========================================================================
   * OAM SPRITE & PARTICLE HELPERS
   * ======================================================================== */
  private drawShadow(ctx: CanvasRenderingContext2D, cx: number, cy: number, w: number): void {
    ctx.fillStyle = 'rgba(18, 22, 32, 0.36)';
    ctx.fillRect(Math.round(cx - w / 2), Math.round(cy - 2), w, 4);
    ctx.fillRect(Math.round(cx - w / 2 + 2), Math.round(cy - 3), w - 4, 6);
  }

  private drawOamSprite(
    ctx: CanvasRenderingContext2D,
    spr: OamSprite,
    atlas: PixelAtlas,
    frameCount: number
  ): void {
    const img = atlas.getSprite(spr.spriteId, spr.dir, spr.frame);

    // Radiant or Hollow ground aura ring for Heart-Mending NPCs!
    if (spr.aura === 'RADIANT_COURAGE' || spr.aura === 'HOLLOW_FEAR' || spr.aura === 'MENDED') {
      const pulse = Math.sin(frameCount * 0.1) * 1.5;
      ctx.save();
      ctx.strokeStyle =
        spr.aura === 'HOLLOW_FEAR'
          ? 'rgba(158, 118, 204, 0.85)'
          : spr.aura === 'RADIANT_COURAGE'
            ? 'rgba(245, 184, 65, 0.9)'
            : 'rgba(116, 180, 97, 0.55)';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.ellipse(spr.x, spr.y - 1, 10 + pulse, 5 + pulse * 0.5, 0, 0, Math.PI * 2);
      ctx.stroke();
      ctx.restore();
    }

    if (spr.shadow) {
      const sw = spr.spriteId === 'boss_briar_warden' ? 24 : 12;
      this.drawShadow(ctx, spr.x, spr.y, sw);
    }

    if (spr.rotationDeg !== 0) {
      ctx.save();
      ctx.translate(spr.x, spr.y);
      ctx.rotate((spr.rotationDeg * Math.PI) / 180);
      ctx.drawImage(img, -Math.round(img.width / 2), -Math.round(img.height / 2));
      ctx.restore();
      return;
    }

    this.drawScaledSprite(
      ctx,
      img,
      spr.x,
      spr.y,
      spr.scaleXFp,
      spr.scaleYFp,
      spr.flipX,
      spr.flashWhite
    );
  }

  private drawScaledSprite(
    ctx: CanvasRenderingContext2D,
    img: HTMLCanvasElement | HTMLImageElement,
    anchorX: number,
    anchorY: number,
    scaleXFp: number,
    scaleYFp: number,
    flipX: boolean,
    flashWhite: boolean
  ): void {
    const w = img.width || 16;
    const h = img.height || 16;
    const drawW = Math.max(4, Math.round((w * scaleXFp) / FP_ONE));
    const drawH = Math.max(4, Math.round((h * scaleYFp) / FP_ONE));

    let source: HTMLCanvasElement | HTMLImageElement = img;
    if (flashWhite) {
      // Crisp 16-bit hit-flash silhouette!
      const sw = Math.min(48, w);
      const sh = Math.min(48, h);
      this.scratchCtx.clearRect(0, 0, 48, 48);
      this.scratchCtx.globalCompositeOperation = 'source-over';
      this.scratchCtx.drawImage(img, 0, 0, sw, sh);
      this.scratchCtx.globalCompositeOperation = 'source-in';
      this.scratchCtx.fillStyle = '#FFFFFF';
      this.scratchCtx.fillRect(0, 0, sw, sh);
      this.scratchCtx.globalCompositeOperation = 'source-over';
      source = this.scratchCanvas;
    }

    ctx.save();
    ctx.translate(Math.round(anchorX), Math.round(anchorY));
    if (flipX) ctx.scale(-1, 1);
    if (flashWhite) {
      ctx.drawImage(source, 0, 0, Math.min(48, w), Math.min(48, h), -Math.round(drawW / 2), -drawH + 2, drawW, drawH);
    } else {
      ctx.drawImage(source, -Math.round(drawW / 2), -drawH + 2, drawW, drawH);
    }
    ctx.restore();
  }

  private renderParticles(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    battleOnly: boolean
  ): void {
    for (const p of packet.particles) {
      if (!p.active) continue;
      if (p.screenSpace !== battleOnly) continue;

      const px = (p.fx >> 8) - (battleOnly ? 0 : packet.camX);
      const py = (p.fy >> 8) - (battleOnly ? 0 : packet.camY);
      const alpha = Math.min(1, p.life / Math.max(1, p.maxLife * 0.45));

      ctx.save();
      ctx.globalAlpha = alpha;
      ctx.fillStyle = p.color;
      ctx.fillRect(px, py, p.size, p.size);
      ctx.restore();
    }
  }

  private renderFloatingTexts(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    worldOffset: boolean
  ): void {
    ctx.save();
    ctx.textBaseline = 'top';
    for (const ft of packet.floatingTexts) {
      if (!ft.active) continue;
      const sx = Math.round(ft.x - (worldOffset ? packet.camX : 0));
      const sy = Math.round(ft.y - (worldOffset ? packet.camY : 0));
      const fontSize = ft.scale > 1.1 ? 10 : 8;
      ctx.font = `bold ${fontSize}px monospace`;
      const tw = ctx.measureText(ft.text).width;
      const dx = Math.round(sx - tw / 2);

      // Crisp 1px pixel outline for instant legibility
      ctx.fillStyle = '#11131A';
      ctx.fillText(ft.text, dx - 1, sy);
      ctx.fillText(ft.text, dx + 1, sy);
      ctx.fillText(ft.text, dx, sy - 1);
      ctx.fillText(ft.text, dx, sy + 1);
      ctx.fillStyle = ft.color;
      ctx.fillText(ft.text, dx, sy);
    }
    ctx.restore();
  }
}
