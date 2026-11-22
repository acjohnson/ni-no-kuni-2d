/**
 * Project Hearth — Software Virtual PPU Renderer (320x224 Genesis / 256x224 SNES)
 *
 * Thin replaceable host renderer that consumes a pure PpuFramePacket from
 * HearthEngine and draws crisp nearest-neighbor 16-bit layers onto the canvas.
 */

import {
  BattleEnemyState,
  FP_ONE,
  LockKind,
  MAP_H,
  MAP_W,
  OamSprite,
  PartyMemberState,
  PpuFramePacket,
  T_LANTERN_POST,
  T_MTN_SKY,
  TILE_SIZE,
  WorldMapData,
} from '../core/types';
import { PixelAtlas } from './pixelAtlas';

export class SoftwarePpuRenderer {
  private scratchCanvas: HTMLCanvasElement;
  private scratchCtx: CanvasRenderingContext2D;
  private darkCanvas: HTMLCanvasElement;
  private darkCtx: CanvasRenderingContext2D;

  constructor() {
    this.scratchCanvas = document.createElement('canvas');
    this.scratchCanvas.width = 48;
    this.scratchCanvas.height = 48;
    this.scratchCtx = this.scratchCanvas.getContext('2d')!;
    this.darkCanvas = document.createElement('canvas');
    this.darkCanvas.width = 320;
    this.darkCanvas.height = 224;
    this.darkCtx = this.darkCanvas.getContext('2d')!;
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

    atlas.setTerrainPalette(packet.terrainPalId);

    const ctx = canvas.getContext('2d');
    if (!ctx) return;
    ctx.imageSmoothingEnabled = false;

    ctx.save();
    ctx.clearRect(0, 0, packet.width, packet.height);
    ctx.translate(packet.shakeX, packet.shakeY);

    if (packet.mode === 'INTRO') {
      this.renderIntroScene(ctx, packet, atlas);
    } else if (
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

    // Hardware-style scene crossfade veil (0..15)
    if (packet.fadeLevel > 0) {
      const alpha = Math.min(1, packet.fadeLevel / 15);
      ctx.fillStyle = `rgba(8, 7, 14, ${alpha.toFixed(3)})`;
      ctx.fillRect(-8, -8, packet.width + 16, packet.height + 16);
    }

    ctx.restore();
  }

  /* ========================================================================
   * OPENING CINEMA BACKDROP (Web renders full-HD painted plates in CinemaTheater)
   * ======================================================================== */
  private renderIntroScene(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    _atlas: PixelAtlas
  ): void {
    ctx.fillStyle = '#08070F';
    ctx.fillRect(0, 0, packet.width, packet.height);
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
    const mapW = map.width || packet.mapW || MAP_W;
    const mapH = map.height || packet.mapH || MAP_H;
    const animTick = frameCount >> 4;

    const startCol = Math.max(0, Math.floor(camX / TILE_SIZE));
    const endCol = Math.min(mapW - 1, Math.ceil((camX + width) / TILE_SIZE));
    const startRow = Math.max(0, Math.floor(camY / TILE_SIZE));
    const endRow = Math.min(mapH - 1, Math.ceil((camY + height) / TILE_SIZE));

    // 0. Parallax Mountain / Castle Aurelia Vista
    if (packet.hasMountainVista) {
      this.renderMountainVista(ctx, packet);
    }

    // 1. BG1 Terrain & Architecture Plane
    for (let r = startRow; r <= endRow; r++) {
      const sy = r * TILE_SIZE - camY;
      const rowOff = r * mapW;
      for (let c = startCol; c <= endCol; c++) {
        const sx = c * TILE_SIZE - camX;
        const tileId = map.bg1[rowOff + c];
        if (tileId === T_MTN_SKY && packet.hasMountainVista) continue;
        const img = atlas.getBg1Tile(tileId, animTick);
        ctx.drawImage(img, sx, sy, TILE_SIZE, TILE_SIZE);
      }
    }

    // 1.5. Impressive Multi-Tile Storybook Landmarks on the Evertale Open World Map!
    if (packet.isWorldMap) {
      this.renderWorldMapLandmarks(ctx, packet, atlas);
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
      const rowOff = r * mapW;
      for (let c = startCol; c <= endCol; c++) {
        const bg2Id = map.bg2[rowOff + c];
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

    // 4.2. Crisp World-Map Landmark Signposts when Rowan approaches a settlement or ruin
    if (packet.isWorldMap && packet.mode === 'EXPLORE') {
      this.renderWorldLandmarkBanners(ctx, packet);
    }

    // 4.5. Fishing Rod Line from Rowan to Bobber during MODE_FISHING
    if (packet.mode === 'FISHING' && packet.fishing.active) {
      const rx = Math.round(packet.heroWorldX - camX);
      const ry = Math.round(packet.heroWorldY - camY - 12);
      const bx = Math.round(packet.fishing.bobberX - camX);
      const by = Math.round(packet.fishing.bobberY - camY - 2);
      ctx.save();
      ctx.strokeStyle = '#E8DEC8';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(rx, ry);
      ctx.quadraticCurveTo(
        Math.round((rx + bx) * 0.5),
        Math.min(ry, by) - 10,
        bx,
        by
      );
      ctx.stroke();
      ctx.restore();
    }

    // 5. World-Space Particles
    this.renderParticles(ctx, packet, false);

    // 5.5. Sunken Sewers Atmospheric Lantern Lighting Mask
    if (packet.zoneIsDark) {
      if (this.darkCanvas.width !== width || this.darkCanvas.height !== height) {
        this.darkCanvas.width = width;
        this.darkCanvas.height = height;
      }
      const dctx = this.darkCtx;
      dctx.globalCompositeOperation = 'source-over';
      dctx.clearRect(0, 0, width, height);
      dctx.fillStyle = packet.hasMountainVista
        ? 'rgba(10, 16, 38, 0.52)'
        : 'rgba(7, 11, 18, 0.76)';
      dctx.fillRect(0, 0, width, height);

      const punchLight = (lx: number, ly: number, r: number) => {
        const g = dctx.createRadialGradient(lx, ly, r * 0.18, lx, ly, r);
        g.addColorStop(0, 'rgba(0, 0, 0, 1)');
        g.addColorStop(0.62, 'rgba(0, 0, 0, 0.72)');
        g.addColorStop(1, 'rgba(0, 0, 0, 0)');
        dctx.fillStyle = g;
        dctx.beginPath();
        dctx.arc(lx, ly, r, 0, Math.PI * 2);
        dctx.fill();
      };

      dctx.globalCompositeOperation = 'destination-out';
      const flicker = Math.sin(frameCount * 0.12) * 3;
      punchLight(packet.heroWorldX - camX, packet.heroWorldY - camY - 6, 62 + flicker);
      for (let r = startRow; r <= endRow; r++) {
        const rowOff = r * mapW;
        for (let c = startCol; c <= endCol; c++) {
          if (map.bg1[rowOff + c] === T_LANTERN_POST) {
            punchLight(c * TILE_SIZE + 8 - camX, r * TILE_SIZE + 6 - camY, 48 + flicker * 0.6);
          }
        }
      }
      dctx.globalCompositeOperation = 'source-over';
      ctx.drawImage(this.darkCanvas, 0, 0);
    }

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

    // 9. Zone Title Ribbon on Area Entry
    if (packet.zoneBannerFrames > 0 && packet.mode === 'EXPLORE' && packet.zoneName) {
      ctx.font = 'bold 8px monospace';
      const label = packet.zoneName.toUpperCase();
      const bw = Math.ceil(ctx.measureText(label).width) + 18;
      const bx = Math.round((width - bw) / 2);
      ctx.fillStyle = 'rgba(16, 18, 30, 0.88)';
      ctx.fillRect(bx, 6, bw, 14);
      ctx.strokeStyle = '#D4A454';
      ctx.lineWidth = 1;
      ctx.strokeRect(bx + 0.5, 6.5, bw - 1, 13);
      ctx.fillStyle = '#FFF3C4';
      ctx.textBaseline = 'top';
      ctx.fillText(label, bx + 9, 9);
    }
  }

  /* ========================================================================
   * PARALLAX SNOW-DUSTED ALPINE MOUNTAIN VISTA (Morning / Afternoon / Dusk / Night)
   * ======================================================================== */
  private renderMountainVista(ctx: CanvasRenderingContext2D, packet: PpuFramePacket): void {
    const { width, camX, camY, frameCount, timeOfDay } = packet;
    const horizonH = Math.min(packet.height, Math.max(88, 128 - Math.floor(camY * 0.25)));

    // 1. Time-of-Day Sky Gradient Bands
    const skyPalettes: readonly (readonly string[])[] = [
      ['#3A5C88', '#6B8EB5', '#B9978A', '#ECC89E'], // 0 = Morning Rose-Gold Dawn
      ['#2E649E', '#528EC4', '#86B8DF', '#D6E2E8'], // 1 = Crisp Autumn Afternoon
      ['#2B204A', '#6A3656', '#C25F4A', '#EEA05B'], // 2 = Copper-Amber Dusk
      ['#0B1228', '#142042', '#1D315C', '#2A4370'], // 3 = Moonlit Indigo Night
    ];
    const bands = skyPalettes[timeOfDay] ?? skyPalettes[1];
    const bandH = Math.ceil(horizonH / 4);
    for (let i = 0; i < 4; i++) {
      ctx.fillStyle = bands[i];
      ctx.fillRect(0, i * bandH, width, bandH + 1);
    }

    // 2. Celestial Body / Drifting Autumn Clouds
    if (timeOfDay === 3) {
      // Twinkling alpine stars + harvest crescent moon
      ctx.fillStyle = '#E8F0FF';
      for (let i = 0; i < 20; i++) {
        const sx = (i * 47 + 13) % width;
        const sy = (i * 19 + 5) % Math.max(24, horizonH - 28);
        if (((frameCount >> 4) + i) % 5 !== 0) {
          ctx.fillRect(sx, sy, 1, 1);
        }
      }
      ctx.fillStyle = '#FFF5D0';
      ctx.fillRect(width - 54, 12, 12, 12);
      ctx.fillRect(width - 52, 10, 8, 16);
      ctx.fillStyle = '#142042';
      ctx.fillRect(width - 49, 11, 7, 10);
    } else {
      const cloudCol = timeOfDay === 2 ? '#F6B882' : timeOfDay === 0 ? '#FBE3C6' : '#F4F7FA';
      ctx.fillStyle = cloudCol;
      for (let i = 0; i < 4; i++) {
        const cx = (((i * 98 + Math.floor(frameCount * 0.08) - Math.floor(camX * 0.06)) % (width + 64)) + (width + 64)) % (width + 64) - 32;
        const cy = 9 + (i & 1) * 11;
        ctx.fillRect(cx, cy, 30, 5);
        ctx.fillRect(cx + 6, cy - 3, 18, 4);
      }
    }

    // 3. Far Snow-Dusted Alpine Peaks (Parallax 0.14x)
    const farPeakCol =
      timeOfDay === 3 ? '#1F2D4E' : timeOfDay === 2 ? '#543554' : timeOfDay === 0 ? '#5E6B8A' : '#586E8C';
    const snowCapCol =
      timeOfDay === 3 ? '#B4C6E6' : timeOfDay === 2 ? '#F4C6A0' : timeOfDay === 0 ? '#FBEAD6' : '#F2F6FA';
    const farScroll = Math.floor(camX * 0.14);
    for (let x = 0; x < width; x += 4) {
      const wx = x + farScroll;
      const ridge =
        Math.abs(((wx % 96) + 96) % 96 - 48) * 0.65 +
        Math.abs((((wx + 34) % 64) + 64) % 64 - 32) * 0.45;
      const peakY = Math.max(12, Math.round(24 + ridge * 0.72));
      ctx.fillStyle = farPeakCol;
      ctx.fillRect(x, peakY, 4, horizonH - peakY + 8);
      if (peakY < 42) {
        ctx.fillStyle = snowCapCol;
        ctx.fillRect(x, peakY, 4, Math.min(10, 42 - peakY));
      }
    }

    // 4. Midground Ridge — Evertale Sunlit Castle Aurelia Skyline vs. Alderbrook Autumn Foothills
    const isEvertaleVista = packet.zoneId === 9 || packet.zoneId === 10;
    const midCol = isEvertaleVista
      ? '#4A8E54'
      : timeOfDay === 3
        ? '#15243A'
        : timeOfDay === 2
          ? '#4A2B36'
          : timeOfDay === 0
            ? '#3E5448'
            : '#355444';
    const midMaple = isEvertaleVista
      ? '#6AB868'
      : timeOfDay === 3
        ? '#3B2846'
        : timeOfDay === 2
          ? '#A64A32'
          : timeOfDay === 0
            ? '#B8663A'
            : '#B84A32';
    const midScroll = Math.floor(camX * 0.28);
    for (let x = 0; x < width; x += 4) {
      const wx = x + midScroll;
      const hill = Math.round(Math.sin(wx * 0.025) * 8 + Math.cos(wx * 0.052) * 4);
      const hy = Math.max(38, (isEvertaleVista ? 58 : 56) + hill);
      ctx.fillStyle = midCol;
      ctx.fillRect(x, hy, 4, horizonH - hy + 8);
      if (((wx >> 3) & 3) === 1) {
        ctx.fillStyle = midMaple;
        ctx.fillRect(x, hy + 2, 4, 6);
      }
    }

    // 5. Distant Gleaming Castle Aurelia Spire Silhouette above Sunhill Market & Castle Gate
    if (isEvertaleVista) {
      const cx = Math.round(width * 0.5 - (camX - 180) * 0.12);
      const cy = 22;
      ctx.fillStyle = '#DCE6F2';
      ctx.fillRect(cx - 32, cy + 20, 64, 24);
      ctx.fillRect(cx - 10, cy + 8, 20, 34);
      ctx.fillRect(cx - 28, cy + 14, 12, 26);
      ctx.fillRect(cx + 16, cy + 14, 12, 26);
      ctx.fillStyle = '#F7FAFF';
      ctx.fillRect(cx - 8, cy + 10, 6, 30);
      ctx.fillRect(cx - 26, cy + 16, 4, 22);
      ctx.fillRect(cx + 18, cy + 16, 4, 22);
      // Azure conical turrets & gold pennants
      ctx.fillStyle = '#3874B8';
      ctx.fillRect(cx - 12, cy + 4, 24, 5);
      ctx.fillRect(cx - 8, cy, 16, 5);
      ctx.fillRect(cx - 4, cy - 4, 8, 5);
      ctx.fillRect(cx - 30, cy + 10, 16, 5);
      ctx.fillRect(cx - 26, cy + 6, 8, 5);
      ctx.fillRect(cx + 14, cy + 10, 16, 5);
      ctx.fillRect(cx + 18, cy + 6, 8, 5);
      const wave = (frameCount >> 4) & 1;
      ctx.fillStyle = '#F5C45A';
      ctx.fillRect(cx - 1, cy - 8, 6 + wave, 3);
      ctx.fillRect(cx - 23, cy + 2, 5, 2);
      ctx.fillRect(cx + 21, cy + 2, 5, 2);
    }
  }

  /* ========================================================================
   * EVERTALE OPEN WORLD MULTI-TILE STORYBOOK LANDMARKS (Chrono Trigger / FFIV Scale)
   * ======================================================================== */
  private renderWorldMapLandmarks(
    ctx: CanvasRenderingContext2D,
    packet: PpuFramePacket,
    atlas: PixelAtlas
  ): void {
    const { width, height, camX, camY, frameCount } = packet;
    const wave = (frameCount >> 4) & 1;
    const inView = (x: number, y: number, w: number, h: number) =>
      x + w >= -16 && x <= width + 16 && y + h >= -16 && y <= height + 16;

    // Helper: storybook miniature timber cottage with terracotta or slate roof
    const drawCottage = (x: number, y: number, w: number, h: number, roofCol: string, roofHi: string) => {
      ctx.fillStyle = 'rgba(18, 26, 20, 0.35)';
      ctx.fillRect(x + 2, y + h - 2, w, 4);
      ctx.fillStyle = '#1C1B24';
      ctx.fillRect(x, y, w, h);
      ctx.fillStyle = '#F2E5C8';
      ctx.fillRect(x + 1, y + Math.floor(h * 0.45), w - 2, h - Math.floor(h * 0.45) - 1);
      ctx.fillStyle = roofCol;
      ctx.fillRect(x - 1, y + 1, w + 2, Math.floor(h * 0.48));
      ctx.fillStyle = roofHi;
      ctx.fillRect(x + 1, y, w - 2, 3);
      ctx.fillStyle = '#6B462B';
      ctx.fillRect(x + Math.floor(w * 0.4), y + h - 6, 4, 5);
      ctx.fillStyle = '#F5C45A';
      ctx.fillRect(x + 3, y + Math.floor(h * 0.58), 3, 3);
    };

    // 1. CASTLE AURELIA — Grand Multi-Spire White-Marble Citadel at tiles (41..47, 9..12)
    {
      const bx = 41 * TILE_SIZE - camX;
      const by = 8 * TILE_SIZE + 2 - camY;
      if (inView(bx, by, 112, 78)) {
        // Citadel shadow & stone plinth
        ctx.fillStyle = 'rgba(18, 28, 36, 0.42)';
        ctx.fillRect(bx + 4, by + 62, 104, 14);
        // Outer white-stone curtain wall & battlements
        ctx.fillStyle = '#202636';
        ctx.fillRect(bx + 6, by + 34, 100, 34);
        ctx.fillStyle = '#C9D5E6';
        ctx.fillRect(bx + 8, by + 36, 96, 30);
        ctx.fillStyle = '#EEF4FC';
        ctx.fillRect(bx + 10, by + 38, 92, 6);
        for (let mx = 8; mx < 102; mx += 10) {
          ctx.fillStyle = '#202636';
          ctx.fillRect(bx + mx, by + 31, 6, 4);
          ctx.fillStyle = '#EEF4FC';
          ctx.fillRect(bx + mx + 1, by + 32, 4, 3);
        }
        // Flanking twin watchtowers (left & right)
        for (const tx of [bx + 8, bx + 84]) {
          ctx.fillStyle = '#202636';
          ctx.fillRect(tx, by + 18, 20, 48);
          ctx.fillStyle = '#D8E3F2';
          ctx.fillRect(tx + 2, by + 20, 16, 44);
          ctx.fillStyle = '#FFFFFF';
          ctx.fillRect(tx + 3, by + 20, 5, 44);
          // Azure conical roof
          ctx.fillStyle = '#1E3C6E';
          ctx.fillRect(tx - 2, by + 13, 24, 6);
          ctx.fillStyle = '#3672B8';
          ctx.fillRect(tx, by + 9, 20, 6);
          ctx.fillStyle = '#5CA4E8';
          ctx.fillRect(tx + 4, by + 4, 12, 6);
          ctx.fillRect(tx + 8, by + 1, 4, 4);
          // Waving golden pennant
          ctx.fillStyle = '#F5C45A';
          ctx.fillRect(tx + 10, by - 3, 7 + wave, 3);
          ctx.fillStyle = '#264470';
          ctx.fillRect(tx + 8, by + 28, 4, 7);
        }
        // Central soaring royal keep & golden dome
        ctx.fillStyle = '#202636';
        ctx.fillRect(bx + 36, by + 8, 40, 56);
        ctx.fillStyle = '#E4ECF8';
        ctx.fillRect(bx + 38, by + 10, 36, 52);
        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect(bx + 40, by + 10, 10, 52);
        ctx.fillStyle = '#1E3C6E';
        ctx.fillRect(bx + 34, by + 3, 44, 7);
        ctx.fillStyle = '#3672B8';
        ctx.fillRect(bx + 40, by - 4, 32, 8);
        ctx.fillStyle = '#5CA4E8';
        ctx.fillRect(bx + 46, by - 10, 20, 7);
        ctx.fillStyle = '#F5C45A';
        ctx.fillRect(bx + 54, by - 15, 4, 6);
        ctx.fillRect(bx + 56, by - 15, 9 + wave, 3);
        // Grand royal archway gate & sunlit rose window
        ctx.fillStyle = '#F5C45A';
        ctx.fillRect(bx + 51, by + 18, 10, 10);
        ctx.fillStyle = '#70C4F8';
        ctx.fillRect(bx + 53, by + 20, 6, 6);
        ctx.fillStyle = '#202636';
        ctx.fillRect(bx + 46, by + 44, 20, 24);
        ctx.fillStyle = '#5E3B24';
        ctx.fillRect(bx + 48, by + 46, 16, 22);
        ctx.fillStyle = '#D49A42';
        ctx.fillRect(bx + 48, by + 46, 16, 2);
        ctx.fillRect(bx + 55, by + 46, 2, 22);
      }
    }

    // 2. SUNHILL MARKET — Cozy Hilltop Medina-Style Market Town at tiles (42..46, 21..23)
    {
      const bx = 42 * TILE_SIZE - camX;
      const by = 20 * TILE_SIZE + 8 - camY;
      if (inView(bx, by, 84, 58)) {
        // Low stone terrace wall & archway
        ctx.fillStyle = '#22202A';
        ctx.fillRect(bx + 2, by + 40, 76, 12);
        ctx.fillStyle = '#D4C8B4';
        ctx.fillRect(bx + 4, by + 42, 72, 8);
        ctx.fillStyle = '#8C7A64';
        ctx.fillRect(bx + 32, by + 41, 16, 11);
        ctx.fillStyle = '#E8D5A8';
        ctx.fillRect(bx + 34, by + 43, 12, 9);
        // Cluster of warm sunlit hilltop cottages
        drawCottage(bx + 4, by + 16, 20, 22, '#C8523E', '#E67658');
        drawCottage(bx + 56, by + 14, 20, 24, '#B84A38', '#DC6B52');
        drawCottage(bx + 26, by + 4, 26, 22, '#3B6EA8', '#5C94CE');
        // Golden archive belltower spire
        ctx.fillStyle = '#20222E';
        ctx.fillRect(bx + 34, by - 4, 10, 10);
        ctx.fillStyle = '#F5C45A';
        ctx.fillRect(bx + 36, by - 2, 6, 6);
        // Red & cream striped market awning stalls flanking the plaza
        for (const ax of [bx + 12, bx + 52]) {
          ctx.fillStyle = '#201C24';
          ctx.fillRect(ax, by + 31, 16, 9);
          ctx.fillStyle = '#D6483C';
          ctx.fillRect(ax + 1, by + 32, 14, 4);
          ctx.fillStyle = '#FFF4DC';
          ctx.fillRect(ax + 4, by + 32, 3, 4);
          ctx.fillRect(ax + 10, by + 32, 3, 4);
          ctx.fillStyle = '#B8844A';
          ctx.fillRect(ax + 2, by + 36, 12, 3);
        }
        // Central sparkling blue fountain glint
        ctx.fillStyle = '#68BBE8';
        ctx.fillRect(bx + 35, by + 32, 10, 6);
        ctx.fillStyle = wave ? '#FFFFFF' : '#B8ECFF';
        ctx.fillRect(bx + 38, by + 30, 4, 4);
      }
    }

    // 3. WINDMILL HAMLET — Western Lakeside Mill Village at tiles (17..19, 26..27)
    {
      const bx = 16 * TILE_SIZE + 8 - camX;
      const by = 25 * TILE_SIZE + 4 - camY;
      if (inView(bx, by, 58, 46)) {
        drawCottage(bx + 2, by + 20, 18, 18, '#B8543E', '#D87258');
        drawCottage(bx + 34, by + 22, 18, 16, '#A84836', '#CE684E');
        // Stone windmill tower
        ctx.fillStyle = '#1E222E';
        ctx.fillRect(bx + 18, by + 6, 18, 32);
        ctx.fillStyle = '#B8C0CC';
        ctx.fillRect(bx + 20, by + 12, 14, 24);
        ctx.fillStyle = '#DCE3EC';
        ctx.fillRect(bx + 21, by + 12, 5, 24);
        ctx.fillStyle = '#3B5998';
        ctx.fillRect(bx + 17, by + 4, 20, 9);
        ctx.fillStyle = '#5C82C4';
        ctx.fillRect(bx + 20, by + 2, 14, 4);
        ctx.fillStyle = '#5E3B24';
        ctx.fillRect(bx + 24, by + 28, 6, 8);
        // Spinning windmill sails
        const sails = atlas.getSprite('windmill_blades', 0, 0);
        ctx.save();
        ctx.translate(bx + 27, by + 14);
        ctx.rotate(((frameCount * 0.9) * Math.PI) / 180);
        ctx.drawImage(sails, -13, -13, 26, 26);
        ctx.restore();
      }
    }

    // 4. HOLLOW OAK GATE — Monumental Golden-Amber Great Tree at tile (16, 43)
    {
      const bx = 15 * TILE_SIZE - camX;
      const by = 41 * TILE_SIZE + 4 - camY;
      if (inView(bx, by, 48, 46)) {
        ctx.fillStyle = 'rgba(18, 28, 22, 0.4)';
        ctx.fillRect(bx + 6, by + 34, 36, 8);
        // Gnarled trunk & glowing portal arch
        ctx.fillStyle = '#1E1818';
        ctx.fillRect(bx + 12, by + 16, 24, 24);
        ctx.fillStyle = '#6E462C';
        ctx.fillRect(bx + 14, by + 18, 20, 20);
        ctx.fillStyle = '#8C5B38';
        ctx.fillRect(bx + 16, by + 18, 5, 18);
        ctx.fillStyle = wave ? '#74E8F5' : '#FFE07D';
        ctx.fillRect(bx + 20, by + 26, 8, 12);
        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect(bx + 22, by + 28, 4, 7);
        // Lush golden-emerald crown
        ctx.fillStyle = '#1E241C';
        ctx.fillRect(bx + 2, by + 2, 44, 20);
        ctx.fillStyle = '#3E7A38';
        ctx.fillRect(bx + 4, by + 4, 40, 16);
        ctx.fillStyle = '#D48A34';
        ctx.fillRect(bx + 8, by + 3, 32, 11);
        ctx.fillStyle = '#F5C45A';
        ctx.fillRect(bx + 14, by + 2, 20, 6);
      }
    }

    // 5. BRAMBLEWOOD CROSSING — Towering Ancient Forest & Standing Stones at tiles (67..69, 23..24)
    {
      const bx = 66 * TILE_SIZE + 8 - camX;
      const by = 22 * TILE_SIZE - camY;
      if (inView(bx, by, 58, 44)) {
        // Overlapping emerald ancient crowns
        const trees: [number, number, number][] = [
          [4, 8, 22],
          [32, 8, 22],
          [16, 0, 26],
        ];
        for (const [tx, ty, sz] of trees) {
          ctx.fillStyle = '#16241B';
          ctx.fillRect(bx + tx, by + ty, sz, sz);
          ctx.fillStyle = '#285C36';
          ctx.fillRect(bx + tx + 2, by + ty + 2, sz - 4, sz - 4);
          ctx.fillStyle = '#448C48';
          ctx.fillRect(bx + tx + 4, by + ty + 3, sz - 8, Math.floor(sz * 0.55));
          ctx.fillStyle = '#74C064';
          ctx.fillRect(bx + tx + 6, by + ty + 3, Math.floor(sz * 0.4), 4);
        }
        // Twin mossy standing stones & emerald trail arch
        ctx.fillStyle = '#20262E';
        ctx.fillRect(bx + 16, by + 24, 7, 16);
        ctx.fillRect(bx + 35, by + 24, 7, 16);
        ctx.fillStyle = '#9EAAB8';
        ctx.fillRect(bx + 17, by + 25, 5, 14);
        ctx.fillRect(bx + 36, by + 25, 5, 14);
        ctx.fillStyle = wave ? '#8AE8A0' : '#5AD2F4';
        ctx.fillRect(bx + 19, by + 29, 2, 5);
        ctx.fillRect(bx + 38, by + 29, 2, 5);
      }
    }

    // 6. SUNKEN WATCHTOWER — Coastal Cliffside Ruins at tiles (65..68, 41..42)
    {
      const bx = 65 * TILE_SIZE - camX;
      const by = 39 * TILE_SIZE + 12 - camY;
      if (inView(bx, by, 66, 46)) {
        // Broken stone arch & crumbling sea tower
        ctx.fillStyle = '#1E222C';
        ctx.fillRect(bx + 6, by + 18, 14, 22);
        ctx.fillRect(bx + 44, by + 22, 14, 18);
        ctx.fillRect(bx + 22, by + 4, 20, 36);
        ctx.fillStyle = '#7F8A9C';
        ctx.fillRect(bx + 8, by + 20, 10, 18);
        ctx.fillRect(bx + 46, by + 24, 10, 14);
        ctx.fillRect(bx + 24, by + 6, 16, 32);
        ctx.fillStyle = '#ABB6C8';
        ctx.fillRect(bx + 25, by + 6, 5, 30);
        // Jagged broken tower top & coastal moss
        ctx.fillStyle = '#4B8E54';
        ctx.fillRect(bx + 24, by + 6, 6, 4);
        ctx.fillRect(bx + 8, by + 20, 6, 4);
        ctx.fillStyle = '#1C222E';
        ctx.fillRect(bx + 28, by + 24, 8, 14);
      }
    }

    // 7. WHISKERBURGH — Northeast Mouse-Borough Canal Town at tiles (70..74, 11..13)
    {
      const bx = 70 * TILE_SIZE - camX;
      const by = 10 * TILE_SIZE + 8 - camY;
      if (inView(bx, by, 80, 52)) {
        drawCottage(bx + 4, by + 16, 20, 20, '#8A4E74', '#B4729B');
        drawCottage(bx + 52, by + 16, 20, 20, '#8A4E74', '#B4729B');
        drawCottage(bx + 24, by + 6, 28, 26, '#D49A38', '#F2C05A');
        // Little round guild dome
        ctx.fillStyle = '#F5C45A';
        ctx.fillRect(bx + 34, by + 2, 8, 5);
        ctx.fillStyle = '#68BBE8';
        ctx.fillRect(bx + 12, by + 38, 52, 5);
      }
    }
  }

  private renderWorldLandmarkBanners(ctx: CanvasRenderingContext2D, packet: PpuFramePacket): void {
    const { width, camX, camY, heroWorldX, heroWorldY } = packet;
    const landmarks: readonly { tx: number; ty: number; name: string }[] = [
      { tx: 16, ty: 44, name: 'HOLLOW OAK GATE' },
      { tx: 18, ty: 28, name: 'WINDMILL HAMLET' },
      { tx: 44, ty: 24, name: 'SUNHILL MARKET' },
      { tx: 44, ty: 13, name: 'CASTLE AURELIA' },
      { tx: 68, ty: 25, name: 'BRAMBLEWOOD' },
      { tx: 66, ty: 43, name: 'SUNKEN WATCHTOWER' },
      { tx: 72, ty: 14, name: 'WHISKERBURGH' },
    ];
    ctx.font = 'bold 8px monospace';
    ctx.textBaseline = 'top';
    for (const lm of landmarks) {
      const lx = lm.tx * TILE_SIZE + 8;
      const ly = lm.ty * TILE_SIZE + 8;
      const dx = Math.abs(heroWorldX - lx);
      const dy = Math.abs(heroWorldY - ly);
      if (dx + dy > 104) continue;
      const tw = Math.ceil(ctx.measureText(lm.name).width) + 10;
      const sx = Math.max(4, Math.min(width - tw - 4, Math.round(lx - camX - tw / 2)));
      const sy = Math.max(4, Math.round(ly - camY - 34));
      ctx.fillStyle = 'rgba(18, 22, 34, 0.88)';
      ctx.fillRect(sx, sy, tw, 12);
      ctx.strokeStyle = '#E8BE5E';
      ctx.lineWidth = 1;
      ctx.strokeRect(sx + 0.5, sy + 0.5, tw - 1, 11);
      ctx.fillStyle = '#FFF5D0';
      ctx.fillText(lm.name, sx + 5, sy + 2);
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
    const biome = battle.biome;
    const isShrine = biome === 'SHRINE';
    const isSewer = biome === 'SEWER';
    const isBoarhold = biome === 'BOARHOLD';

    // 1. Layered 16-bit Sky & Horizon Backdrop (cohesive with world biome!)
    const skyBands = isSewer
      ? ['#0C141A', '#13212A', '#1B2E38', '#233B44']
      : isBoarhold
        ? ['#241418', '#422024', '#68302A', '#924834']
        : isShrine
          ? ['#1D2236', '#28314C', '#384666', '#4E5D7B']
          : ['#5AA0D8', '#7BBCE8', '#A6D7F2', '#DCEFC8'];
    const bandH = 19;
    skyBands.forEach((col, i) => {
      ctx.fillStyle = col;
      ctx.fillRect(0, i * bandH, width, bandH + 1);
    });

    if (isSewer) {
      // Arched sewer vault bricks & glowing lantern sconces
      ctx.fillStyle = '#16252E';
      for (let x = 0; x < width; x += 64) {
        ctx.fillRect(x, 0, 14, 78);
        ctx.fillStyle = '#2B424D';
        ctx.fillRect(x + 2, 0, 4, 78);
        ctx.fillStyle = '#F5C45A';
        ctx.fillRect(x + 4, 28, 6, 8);
        ctx.fillStyle = '#16252E';
      }
    } else if (isBoarhold) {
      // Fortress crenellations & torchlight braziers
      ctx.fillStyle = '#342426';
      ctx.fillRect(0, 44, width, 34);
      for (let x = 0; x < width; x += 24) {
        ctx.fillRect(x, 34, 14, 12);
      }
      ctx.fillStyle = '#E88838';
      ctx.fillRect(46, 26, 6, 8);
      ctx.fillRect(width - 54, 26, 6, 8);
    } else {
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
        ctx.fillStyle = '#3E4656';
        ctx.fillRect(40, 34, 14, 48);
        ctx.fillRect(width - 62, 30, 16, 52);
        ctx.fillStyle = '#748094';
        ctx.fillRect(42, 36, 4, 44);
        ctx.fillRect(width - 60, 32, 5, 48);
      } else {
        ctx.fillStyle = '#6A7586';
        ctx.fillRect(width - 78, 36, 16, 36);
        ctx.fillStyle = '#3B5998';
        ctx.fillRect(width - 80, 30, 20, 8);
        const sails = atlas.getSprite('windmill_blades', 0, 0);
        ctx.save();
        ctx.translate(width - 70, 36);
        ctx.rotate(((frameCount * 0.8) * Math.PI) / 180);
        ctx.drawImage(sails, -12, -12, 24, 24);
        ctx.restore();
      }
    }

    // 2. Ground Battle Stage Plane
    const groundY = 76;
    ctx.fillStyle = isSewer
      ? '#21333B'
      : isBoarhold
        ? '#584440'
        : isShrine
          ? '#3D4555'
          : '#5EA055';
    ctx.fillRect(0, groundY, width, height - groundY);

    // Textured perspective strips & biome floor landmarks
    if (isSewer) {
      // Glowing moss-green water canal across top of arena
      ctx.fillStyle = '#1E645C';
      ctx.fillRect(0, groundY + 4, width, 16);
      ctx.fillStyle = (frameCount >> 4) & 1 ? '#58C4A0' : '#3C9E82';
      ctx.fillRect(0, groundY + 10, width, 2);
      ctx.fillStyle = '#33464E';
      ctx.fillRect(0, groundY + 24, width, 66);
      ctx.fillStyle = '#172329';
      ctx.fillRect(0, 166, width, height - 166);
    } else if (isBoarhold) {
      ctx.fillStyle = '#7A5B50';
      ctx.fillRect(0, groundY + 16, width, 70);
      ctx.fillStyle = '#A86E42';
      ctx.fillRect(0, groundY + 16, width, 3);
      ctx.fillRect(0, groundY + 84, width, 3);
      ctx.fillStyle = '#3A2828';
      ctx.fillRect(0, 166, width, height - 166);
    } else if (isShrine) {
      ctx.fillStyle = '#535E72';
      ctx.fillRect(0, groundY + 14, width, 74);
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

    // 6.5. Sea-of-Stars Choreographed Attack & Parry VFX
    this.renderBattleVfx(ctx, packet);

    // 7. Battle Screen-Space Particles
    this.renderParticles(ctx, packet, true);

    // 8. Diegetic Sea of Stars-Style Timed Hit / Parry Convergence Ring!
    if (battle.timedRing.active) {
      const tr = battle.timedRing;
      const isParry = tr.kind === 'PARRY';
      const inSweet = tr.progress >= tr.sweetStart && tr.progress <= tr.sweetEnd;
      const baseR = 10;
      const shrinkR = Math.max(baseR - 2, Math.round(baseR + (1 - Math.min(1.06, tr.progress)) * 26));

      ctx.save();
      if (inSweet) {
        ctx.fillStyle = isParry ? 'rgba(90, 210, 244, 0.25)' : 'rgba(245, 184, 65, 0.28)';
        ctx.beginPath();
        ctx.arc(tr.x, tr.y, baseR + 2, 0, Math.PI * 2);
        ctx.fill();
      }
      // Target inner anchor circle
      ctx.strokeStyle = isParry ? 'rgba(90, 210, 244, 0.9)' : 'rgba(245, 184, 65, 0.9)';
      ctx.lineWidth = 1.5;
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
        const cue = isParry ? 'PARRY!' : 'STRIKE!';
        const cw = ctx.measureText(cue).width;
        ctx.fillStyle = '#141824';
        ctx.fillRect(Math.round(tr.x - cw / 2 - 4), Math.round(tr.y - 24), Math.ceil(cw + 8), 11);
        ctx.strokeStyle = isParry ? '#5AD2F4' : '#FFF1A8';
        ctx.lineWidth = 1;
        ctx.strokeRect(Math.round(tr.x - cw / 2 - 4) + 0.5, Math.round(tr.y - 24) + 0.5, Math.ceil(cw + 7), 10);
        ctx.fillStyle = isParry ? '#5AD2F4' : '#FFF1A8';
        ctx.textBaseline = 'top';
        ctx.fillText(cue, Math.round(tr.x - cw / 2), Math.round(tr.y - 22));
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

  private renderBattleVfx(ctx: CanvasRenderingContext2D, packet: PpuFramePacket): void {
    const v = packet.battle.vfx;
    if (v.kind === 'NONE' || v.dur <= 0) return;
    const p = Math.max(0, Math.min(1, (v.dur - v.timer) / v.dur));
    const cx = v.x + (v.tx - v.x) * p;
    const cy = v.y + (v.ty - v.y) * p;

    ctx.save();
    switch (v.kind) {
      case 'STAFF_ARC': {
        const arcY = cy - Math.sin(p * Math.PI) * 16;
        ctx.strokeStyle = '#FFF6CC';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(cx, arcY, 10 + p * 8, -Math.PI * 0.75, Math.PI * 0.25);
        ctx.stroke();
        ctx.strokeStyle = '#F5B841';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(cx, arcY, 7 + p * 6, -Math.PI * 0.65, Math.PI * 0.35);
        ctx.stroke();
        break;
      }
      case 'ACORN_SPIN': {
        const ang = p * Math.PI * 6;
        ctx.translate(cx, cy);
        ctx.rotate(ang);
        ctx.strokeStyle = '#8CD674';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.arc(0, 0, 12, 0, Math.PI * 1.5);
        ctx.stroke();
        ctx.strokeStyle = '#FFF1A8';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.moveTo(-11, 0);
        ctx.lineTo(11, 0);
        ctx.moveTo(0, -11);
        ctx.lineTo(0, 11);
        ctx.stroke();
        break;
      }
      case 'SOLAR_METEOR': {
        const my = cy - Math.sin(p * Math.PI) * 52;
        ctx.fillStyle = 'rgba(232, 106, 74, 0.45)';
        ctx.beginPath();
        ctx.arc(cx, my, 15, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = '#FFE066';
        ctx.beginPath();
        ctx.arc(cx, my, 8, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFF6CC';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(cx, my, 17 + (packet.frameCount & 3), 0, Math.PI * 2);
        ctx.stroke();
        if (p > 0.8) {
          const shock = (p - 0.8) * 140;
          ctx.strokeStyle = '#FFE066';
          ctx.lineWidth = 2;
          ctx.beginPath();
          ctx.ellipse(v.tx, v.ty + 4, shock + 6, shock * 0.45 + 3, 0, 0, Math.PI * 2);
          ctx.stroke();
        }
        break;
      }
      case 'STAR_BOLT': {
        const by = cy - Math.sin(p * Math.PI) * 28;
        ctx.strokeStyle = 'rgba(90, 210, 244, 0.7)';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.moveTo(v.x, v.y);
        ctx.quadraticCurveTo((v.x + v.tx) * 0.5, Math.min(v.y, v.ty) - 34, cx, by);
        ctx.stroke();
        ctx.fillStyle = 'rgba(90, 210, 244, 0.45)';
        ctx.beginPath();
        ctx.arc(cx, by, 9, 0, Math.PI * 2);
        ctx.fill();
        ctx.fillStyle = '#FFFFFF';
        ctx.fillRect(Math.round(cx - 3), Math.round(by - 3), 6, 6);
        ctx.strokeStyle = '#FFF6CC';
        ctx.lineWidth = 1;
        ctx.beginPath();
        ctx.moveTo(cx - 9, by);
        ctx.lineTo(cx + 9, by);
        ctx.moveTo(cx, by - 9);
        ctx.lineTo(cx, by + 9);
        ctx.stroke();
        break;
      }
      case 'PRISM_NOVA': {
        const r1 = 8 + p * 34;
        const r2 = 4 + p * 22;
        ctx.fillStyle = `rgba(90, 210, 244, ${(0.35 * (1 - p)).toFixed(2)})`;
        ctx.beginPath();
        ctx.arc(v.tx, v.ty, r2, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFF1A8';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.arc(v.tx, v.ty, r1, 0, Math.PI * 2);
        ctx.stroke();
        ctx.strokeStyle = '#5AD2F4';
        ctx.beginPath();
        ctx.moveTo(v.tx - r1, v.ty);
        ctx.lineTo(v.tx + r1, v.ty);
        ctx.moveTo(v.tx, v.ty - r1);
        ctx.lineTo(v.tx, v.ty + r1);
        ctx.stroke();
        break;
      }
      case 'ENEMY_CLAW': {
        ctx.strokeStyle = '#E85272';
        ctx.lineWidth = 2;
        for (let k = -1; k <= 1; k++) {
          ctx.beginPath();
          ctx.moveTo(cx - 10, cy - 8 + k * 4);
          ctx.lineTo(cx + 10, cy + 8 + k * 4);
          ctx.stroke();
        }
        break;
      }
      case 'GUARD_SHIELD': {
        const r = 14 + p * 10;
        ctx.fillStyle = `rgba(90, 210, 244, ${(0.38 * (1 - p * 0.5)).toFixed(2)})`;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y, r - 2, r + 2, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFFFFF';
        ctx.lineWidth = 2;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y, r, r + 3, 0, 0, Math.PI * 2);
        ctx.stroke();
        break;
      }
      case 'HEAL_BLOOM': {
        const r = 10 + p * 20;
        ctx.fillStyle = `rgba(116, 214, 128, ${(0.32 * (1 - p)).toFixed(2)})`;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y, r, r * 0.55, 0, 0, Math.PI * 2);
        ctx.fill();
        ctx.strokeStyle = '#FFF1A8';
        ctx.lineWidth = 1.5;
        ctx.beginPath();
        ctx.ellipse(v.x, v.y - p * 18, r * 0.7, r * 0.35, 0, 0, Math.PI * 2);
        ctx.stroke();
        break;
      }
      default:
        break;
    }
    ctx.restore();
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
      const sw =
        spr.spriteId === 'boss_briar_warden' ||
        spr.spriteId === 'boss_muckmaw' ||
        spr.spriteId === 'boss_ironfang'
          ? 24
          : spr.spriteId === 'hollow_oak'
            ? 20
            : spr.spriteId === 'pet_milo' || spr.spriteId === 'bobber'
              ? 8
              : 12;
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
