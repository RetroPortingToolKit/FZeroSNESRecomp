#pragma once
/* Private headless presentation check; no pad ownership, actor progress or
 * results are injected. All captures use the real loaded course/renderer. */
static bool feedback_picture(const char *folder,const char *name,const uint32_t *p,unsigned w,unsigned h) {
  char path[2048];snprintf(path,sizeof(path),"%s/%s.ppm",folder,name);
  FILE *f=fopen(path,"wb");if(!f)return false;fprintf(f,"P6\n%u %u\n255\n",w,h);
  for(unsigned i=0;i<w*h;++i) {
    uint8_t rgb[]={(uint8_t)(p[i]>>16),(uint8_t)(p[i]>>8),(uint8_t)p[i]};
    if(fwrite(rgb,3,1,f)!=1){fclose(f);return false;}
  }
  return !fclose(f);
}
static bool feedback_check(void) {
  static bool done;const char *folder=getenv("FZERO_TEST_FEEDBACK");
  if(!folder || done)return true;
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(FzeroLocalSession());
  if(!s || s->phase!=FZERO_VS_RACING || s->race_tick<60)return true;
  unsigned saved_owner=FzeroLocalMenuOwner(),saved_selection=FzeroLocalMenuSelection();
  uint64_t hash=FzeroVersusHash(FzeroLocalSession());
  for(unsigned wide=0;wide<2;++wide)for(unsigned scale=1;scale<=2;++scale) {
    FzeroVideoSettings settings;FzeroVideoStock(&settings);
    settings.enhanced=true;settings.aspect=wide?FZERO_ASPECT_16_9:FZERO_ASPECT_STOCK;
    FzeroViewport viewport=FzeroCalculateViewport(&settings,wide?1280:640,wide?720:480);
    unsigned width=viewport.width*scale,height=448*scale;size_t size=(size_t)width*height;
    uint32_t *before=calloc(size,sizeof(*before)),*after=calloc(size,sizeof(*after));
    if(!before || !after){free(before);free(after);return false;}
    FzeroLocalSetMenuOwner(0);
    bool ok=FzeroLocalDraw(before,size,viewport,1,scale);
    char name[96];snprintf(name,sizeof(name),"hud-%u-%ux",viewport.width,scale);
    if(ok)ok=feedback_picture(folder,name,before,width,height);
    for(unsigned owner=1;ok && owner<=2;++owner) {
      FzeroLocalSetMenuOwner(owner);
      ok=FzeroLocalDraw(after,size,viewport,1,scale);
      size_t changed=0;
      for(unsigned y=0;ok && y<height;++y)for(unsigned x=0;x<width;++x) {
        bool panel=y>=(4+(owner-1)*224)*scale && y<(76+(owner-1)*224)*scale &&
            x>=(viewport.width-184)*scale && x<(viewport.width-8)*scale;
        if(before[(size_t)y*width+x]!=after[(size_t)y*width+x]) {
          ++changed;if(!panel){ok=false;break;}
        }
      }
      ok=ok && changed>100;
      if(ok && scale==1) {
        snprintf(name,sizeof(name),"live-menu-%u-owner%u",viewport.width,owner);
        ok=feedback_picture(folder,name,after,width,height);
      }
    }
    free(before);free(after);if(!ok)return false;
  }
  FzeroLocalSetMenuOwner(saved_owner);FzeroLocalSetMenuSelection(saved_selection);
  if(FzeroVersusHash(FzeroLocalSession())!=hash)return false;
  fprintf(stderr,"feedback-hud: PASS native/wide 1x/2x; operator-only compact menu, pixels outside panel unchanged, immutable race\n");
  done=true;return true;
}
static bool feedback_audio(long frame) {
  static FILE *trace;const char *folder=getenv("FZERO_TEST_FEEDBACK");
  if(!folder)return true;
  if(!trace) {
    char path[2048];snprintf(path,sizeof(path),"%s/sound.csv",folder);
    trace=fopen(path,"w");if(!trace)return false;
    fputs("frame,tick,phase,countdown,race,p1_status,p2_status,p1_speed,p2_speed,p1_effect,p2_effect,port0,port1,port2,env6,env7\n",trace);
  }
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(FzeroLocalSession());
  if(!s)return true;
  fprintf(trace,"%ld,%llu,%u,%u,%llu,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u,%u\n",frame,
      (unsigned long long)s->tick,s->phase,s->countdown,(unsigned long long)s->race_tick,
      s->racers[0].status,s->racers[1].status,s->racers[0].actor.speed,s->racers[1].actor.speed,
      s->racers[0].actor.sound_flags2,s->racers[1].actor.sound_flags2,g_snes->apu->inPorts[0],
      g_snes->apu->inPorts[1],g_snes->apu->inPorts[2],dsp_read(g_snes->apu->dsp,0x68),dsp_read(g_snes->apu->dsp,0x78));
  return !ferror(trace);
}
