/**
 * Furnace Tracker - multi-system chiptune tracker
 * Copyright (C) 2021-2022 tildearrow and contributors
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "namcowsg.h"
#include "../engine.h"
#include <math.h>

//#define rWrite(a,v) pendingWrites[a]=v;
#define rWrite(a,v) if (!skipRegisterWrites) {writes.emplace(a,v); if (dumpWrites) {addWrite(a,v);} }

#define CHIP_FREQBASE 524288

const char* regCheatSheetNamcoWSG[]={
  "Select", "0",
  "MasterVol", "1",
  "FreqL", "2",
  "FreqH", "3",
  "DataCtl", "4",
  "ChanVol", "5",
  "WaveCtl", "6",
  "NoiseCtl", "7",
  "LFOFreq", "8",
  "LFOCtl", "9",
  NULL
};

const char** DivPlatformNamcoWSG::getRegisterSheet() {
  return regCheatSheetNamcoWSG;
}

const char* DivPlatformNamcoWSG::getEffectName(unsigned char effect) {
  switch (effect) {
    case 0x10:
      return "10xx: Change waveform";
      break;
    case 0x11:
      return "11xx: Toggle noise mode";
      break;
  }
  return NULL;
}

void DivPlatformNamcoWSG::acquire(short* bufL, short* bufR, size_t start, size_t len) {
  short* buf[2]={
    bufL+start, bufR+start
  };
  while (!writes.empty()) {
    QueuedWrite w=writes.front();
    switch (devType) {
      case 1:
        ((namco_device*)namco)->pacman_sound_w(w.addr,w.val);
        break;
      case 2:
        ((namco_device*)namco)->polepos_sound_w(w.addr,w.val);
        break;
      case 15:
        ((namco_15xx_device*)namco)->sharedram_w(w.addr,w.val);
        break;
      case 30:
        ((namco_cus30_device*)namco)->namcos1_cus30_w(w.addr,w.val);
        break;
    }
    regPool[w.addr]=w.val;
    writes.pop();
  }
  namco->sound_stream_update(buf,len);
}

void DivPlatformNamcoWSG::updateWave(int ch) {
  printf("UPDATE NAMCO WAVE\n");
  for (int i=0; i<32; i++) {
    namco->update_namco_waveform(i+ch*32,chan[ch].ws.output[i]);
  }
}

void DivPlatformNamcoWSG::tick(bool sysTick) {
  for (int i=0; i<chans; i++) {
    chan[i].std.next();
    if (chan[i].std.vol.had) {
      chan[i].outVol=((chan[i].vol&15)*MIN(15,chan[i].std.vol.val))>>4;
      //chWrite(i,0x04,0x80|chan[i].outVol);
    }
    if (chan[i].std.duty.had && i>=4) {
      chan[i].noise=chan[i].std.duty.val;
      chan[i].freqChanged=true;
    }
    if (chan[i].std.arp.had) {
      if (!chan[i].inPorta) {
        if (chan[i].std.arp.mode) {
          chan[i].baseFreq=NOTE_FREQUENCY(chan[i].std.arp.val);
        } else {
          chan[i].baseFreq=NOTE_FREQUENCY(chan[i].note+chan[i].std.arp.val);
        }
      }
      chan[i].freqChanged=true;
    } else {
      if (chan[i].std.arp.mode && chan[i].std.arp.finished) {
        chan[i].baseFreq=NOTE_FREQUENCY(chan[i].note);
        chan[i].freqChanged=true;
      }
    }
    if (chan[i].std.wave.had) {
      if (chan[i].wave!=chan[i].std.wave.val || chan[i].ws.activeChanged()) {
        chan[i].wave=chan[i].std.wave.val;
        chan[i].ws.changeWave1(chan[i].wave);
        if (!chan[i].keyOff) chan[i].keyOn=true;
      }
    }
    if (chan[i].std.panL.had) {
      chan[i].pan&=0x0f;
      chan[i].pan|=(chan[i].std.panL.val&15)<<4;
    }
    if (chan[i].std.panR.had) {
      chan[i].pan&=0xf0;
      chan[i].pan|=chan[i].std.panR.val&15;
    }
    if (chan[i].std.panL.had || chan[i].std.panR.had) {
      //chWrite(i,0x05,isMuted[i]?0:chan[i].pan);
    }
    if (chan[i].std.pitch.had) {
      if (chan[i].std.pitch.mode) {
        chan[i].pitch2+=chan[i].std.pitch.val;
        CLAMP_VAR(chan[i].pitch2,-2048,2048);
      } else {
        chan[i].pitch2=chan[i].std.pitch.val;
      }
      chan[i].freqChanged=true;
    }
    if (chan[i].active) {
      if (chan[i].ws.tick() || (chan[i].std.phaseReset.had && chan[i].std.phaseReset.val==1)) {
        updateWave(i);
      }
    }
    if (chan[i].freqChanged || chan[i].keyOn || chan[i].keyOff) {
      //DivInstrument* ins=parent->getIns(chan[i].ins,DIV_INS_PCE);
      chan[i].freq=parent->calcFreq(chan[i].baseFreq,chan[i].pitch,false,2,chan[i].pitch2,chipClock,CHIP_FREQBASE);
      printf("f: %d\n",chan[i].freq);
      if (chan[i].freq>1048575) chan[i].freq=1048575;
      if (chan[i].keyOn) {
      }
      if (chan[i].keyOff) {
      }
      if (chan[i].keyOn) chan[i].keyOn=false;
      if (chan[i].keyOff) chan[i].keyOff=false;
      chan[i].freqChanged=false;
    }
  }

  // update state
  rWrite(0x15,chan[0].outVol);
  rWrite(0x1a,chan[1].outVol);
  rWrite(0x1f,chan[2].outVol);
  printf("%d %d %d\n",chan[0].outVol,chan[1].outVol,chan[2].outVol);

  rWrite(0x10,(chan[0].freq)&15);
  rWrite(0x11,(chan[0].freq>>4)&15);
  rWrite(0x12,(chan[0].freq>>8)&15);
  rWrite(0x13,(chan[0].freq>>12)&15);
  rWrite(0x14,(chan[0].freq>>16)&15);

  rWrite(0x16,(chan[1].freq>>4)&15);
  rWrite(0x17,(chan[1].freq>>8)&15);
  rWrite(0x18,(chan[1].freq>>12)&15);
  rWrite(0x19,(chan[1].freq>>16)&15);

  rWrite(0x1b,(chan[2].freq>>4)&15);
  rWrite(0x1c,(chan[2].freq>>8)&15);
  rWrite(0x1d,(chan[2].freq>>12)&15);
  rWrite(0x1e,(chan[2].freq>>16)&15);

  rWrite(0x05,0);
  rWrite(0x0a,1);
  rWrite(0x0f,2);
}

int DivPlatformNamcoWSG::dispatch(DivCommand c) {
  switch (c.cmd) {
    case DIV_CMD_NOTE_ON: {
      DivInstrument* ins=parent->getIns(chan[c.chan].ins,DIV_INS_PCE);
      if (c.value!=DIV_NOTE_NULL) {
        chan[c.chan].baseFreq=NOTE_FREQUENCY(c.value);
        chan[c.chan].freqChanged=true;
        chan[c.chan].note=c.value;
      }
      chan[c.chan].active=true;
      chan[c.chan].keyOn=true;
      chan[c.chan].macroInit(ins);
      if (chan[c.chan].wave<0) {
        chan[c.chan].wave=0;
        chan[c.chan].ws.changeWave1(chan[c.chan].wave);
      }
      chan[c.chan].ws.init(ins,32,15,chan[c.chan].insChanged);
      chan[c.chan].insChanged=false;
      break;
    }
    case DIV_CMD_NOTE_OFF:
      chan[c.chan].active=false;
      chan[c.chan].keyOff=true;
      chan[c.chan].macroInit(NULL);
      break;
    case DIV_CMD_NOTE_OFF_ENV:
    case DIV_CMD_ENV_RELEASE:
      chan[c.chan].std.release();
      break;
    case DIV_CMD_INSTRUMENT:
      if (chan[c.chan].ins!=c.value || c.value2==1) {
        chan[c.chan].ins=c.value;
        chan[c.chan].insChanged=true;
      }
      break;
    case DIV_CMD_VOLUME:
      if (chan[c.chan].vol!=c.value) {
        chan[c.chan].vol=c.value;
        if (!chan[c.chan].std.vol.has) {
          chan[c.chan].outVol=c.value;
          //if (chan[c.chan].active) chWrite(c.chan,0x04,0x80|chan[c.chan].outVol);
        }
      }
      break;
    case DIV_CMD_GET_VOLUME:
      if (chan[c.chan].std.vol.has) {
        return chan[c.chan].vol;
      }
      return chan[c.chan].outVol;
      break;
    case DIV_CMD_PITCH:
      chan[c.chan].pitch=c.value;
      chan[c.chan].freqChanged=true;
      break;
    case DIV_CMD_WAVE:
      chan[c.chan].wave=c.value;
      chan[c.chan].ws.changeWave1(chan[c.chan].wave);
      chan[c.chan].keyOn=true;
      break;
    case DIV_CMD_NOTE_PORTA: {
      int destFreq=NOTE_FREQUENCY(c.value2);
      bool return2=false;
      if (destFreq>chan[c.chan].baseFreq) {
        chan[c.chan].baseFreq+=c.value;
        if (chan[c.chan].baseFreq>=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      } else {
        chan[c.chan].baseFreq-=c.value;
        if (chan[c.chan].baseFreq<=destFreq) {
          chan[c.chan].baseFreq=destFreq;
          return2=true;
        }
      }
      chan[c.chan].freqChanged=true;
      if (return2) {
        chan[c.chan].inPorta=false;
        return 2;
      }
      break;
    }
    case DIV_CMD_STD_NOISE_MODE:
      chan[c.chan].noise=c.value;
      //chWrite(c.chan,0x07,chan[c.chan].noise?(0x80|chan[c.chan].note):0);
      break;
    case DIV_CMD_PANNING: {
      chan[c.chan].pan=(c.value&0xf0)|(c.value2>>4);
      //chWrite(c.chan,0x05,isMuted[c.chan]?0:chan[c.chan].pan);
      break;
    }
    case DIV_CMD_LEGATO:
      chan[c.chan].baseFreq=NOTE_FREQUENCY(c.value+((chan[c.chan].std.arp.will && !chan[c.chan].std.arp.mode)?(chan[c.chan].std.arp.val):(0)));
      chan[c.chan].freqChanged=true;
      chan[c.chan].note=c.value;
      break;
    case DIV_CMD_PRE_PORTA:
      if (chan[c.chan].active && c.value2) {
        if (parent->song.resetMacroOnPorta) chan[c.chan].macroInit(parent->getIns(chan[c.chan].ins,DIV_INS_PCE));
      }
      chan[c.chan].inPorta=c.value;
      break;
    case DIV_CMD_GET_VOLMAX:
      return 15;
      break;
    case DIV_ALWAYS_SET_VOLUME:
      return 1;
      break;
    default:
      break;
  }
  return 1;
}

void DivPlatformNamcoWSG::muteChannel(int ch, bool mute) {
  isMuted[ch]=mute;
  //chWrite(ch,0x05,isMuted[ch]?0:chan[ch].pan);
}

void DivPlatformNamcoWSG::forceIns() {
  for (int i=0; i<chans; i++) {
    chan[i].insChanged=true;
    chan[i].freqChanged=true;
    updateWave(i);
    //chWrite(i,0x05,isMuted[i]?0:chan[i].pan);
  }
}

void* DivPlatformNamcoWSG::getChanState(int ch) {
  return &chan[ch];
}

DivDispatchOscBuffer* DivPlatformNamcoWSG::getOscBuffer(int ch) {
  return oscBuf[ch];
}

unsigned char* DivPlatformNamcoWSG::getRegisterPool() {
  return regPool;
}

int DivPlatformNamcoWSG::getRegisterPoolSize() {
  return 112;
}

void DivPlatformNamcoWSG::reset() {
  while (!writes.empty()) writes.pop();
  memset(regPool,0,128);
  for (int i=0; i<chans; i++) {
    chan[i]=DivPlatformNamcoWSG::Channel();
    chan[i].std.setEngine(parent);
    chan[i].ws.setEngine(parent);
    chan[i].ws.init(NULL,32,15,false);
  }
  if (dumpWrites) {
    addWrite(0xffffffff,0);
  }
  // TODO: wave memory
  namco->set_voices(chans);
  namco->set_stereo((devType==2 || devType==30));
  namco->device_start(NULL);
  lastPan=0xff;
  cycles=0;
  curChan=-1;
}

bool DivPlatformNamcoWSG::isStereo() {
  return (devType==30);
}

bool DivPlatformNamcoWSG::keyOffAffectsArp(int ch) {
  return true;
}

void DivPlatformNamcoWSG::notifyWaveChange(int wave) {
  for (int i=0; i<chans; i++) {
    if (chan[i].wave==wave) {
      chan[i].ws.changeWave1(wave);
      updateWave(i);
    }
  }
}

void DivPlatformNamcoWSG::notifyInsDeletion(void* ins) {
  for (int i=0; i<chans; i++) {
    chan[i].std.notifyInsDeletion((DivInstrument*)ins);
  }
}

void DivPlatformNamcoWSG::setDeviceType(int type) {
  devType=type;
  switch (type) {
    case 15:
      chans=8;
      for (int i=0; i<8; i++) {
        regVolume[i]=(i<<3)+0x03;
        regFreq[i]=(i<<3)+0x04;
        regWaveSel[i]=(i<<3)+0x06;
      }
      break;
    case 30:
      chans=8;
      for (int i=0; i<8; i++) {
        regVolume[i]=(i<<3);
        regFreq[i]=(i<<3)+0x01;
        regWaveSel[i]=(i<<3)+0x01;
        regVolumeR[i]=(i<<4)+0x04;
        regNoise[i]=(((i+7)&7)<<4)+0x04;
      }
      break;
    case 1:
      chans=3;
      break;
    case 2:
      chans=8;
      for (int i=0; i<8; i++) {
        regVolume[i]=(i<<2)+0x23;
        regVolumeR[i]=(i<<2)+0x02;
        regFreq[i]=(i<<2);
        regWaveSel[i]=(i<<2)+0x23;
      }
      break;
  }
}

void DivPlatformNamcoWSG::setFlags(unsigned int flags) {
  chipClock=3072000;
  rate=chipClock/16;
  namco->device_clock_changed(rate);
  for (int i=0; i<chans; i++) {
    oscBuf[i]->rate=rate;
  }
}

void DivPlatformNamcoWSG::poke(unsigned int addr, unsigned short val) {
  rWrite(addr,val);
}

void DivPlatformNamcoWSG::poke(std::vector<DivRegWrite>& wlist) {
  for (DivRegWrite& i: wlist) rWrite(i.addr,i.val);
}

int DivPlatformNamcoWSG::init(DivEngine* p, int channels, int sugRate, unsigned int flags) {
  parent=p;
  dumpWrites=false;
  skipRegisterWrites=false;
  for (int i=0; i<chans; i++) {
    isMuted[i]=false;
    oscBuf[i]=new DivDispatchOscBuffer;
  }
  switch (devType) {
    case 15:
      namco=new namco_15xx_device(3072000);
      break;
    case 30:
      namco=new namco_cus30_device(3072000);
      break;
    default:
      namco=new namco_device(3072000);
      break;
  }
  setFlags(flags);
  reset();
  return 6;
}

void DivPlatformNamcoWSG::quit() {
  for (int i=0; i<chans; i++) {
    delete oscBuf[i];
  }
  delete namco;
}

DivPlatformNamcoWSG::~DivPlatformNamcoWSG() {
}