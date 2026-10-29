#pragma once
/* Opt-in private runtime fixture: real ordered crossings award every lap.
 * Check both actual panes against the native-reference-qualified OBJ helper,
 * and retain pictures at the award and FINAL LAP's show/hide boundaries. */
static bool lap_cues_check(long frame) {
  const char *folder=getenv("FZERO_TEST_LAP_CUES");
  static unsigned seen[2][2];
  if(!folder)return true;
  const FzeroVersusSnapshot *s=FzeroVersusCompleted(FzeroLocalSession());
  if(!s || s->phase!=FZERO_VS_RACING)return true;
  const unsigned ages[]={0,22,32,128,160};
  for(unsigned player=0;player<2;++player) {
    const FzeroVersusRacer *r=&s->racers[player];
    if(r->status!=FZERO_VS_ACTIVE || (r->completed_laps!=1 && r->completed_laps!=4))continue;
    uint64_t age=s->race_tick-r->lap_start;unsigned final=r->completed_laps==4;
    for(unsigned n=0;n<sizeof(ages)/sizeof(*ages);++n) {
      if(age!=ages[n] || (seen[player][final]&(1u<<n)))continue;
      uint64_t hash=FzeroVersusHash(FzeroLocalSession());
      for(unsigned wide=0;wide<2;++wide) {
        FzeroVideoSettings settings;FzeroVideoStock(&settings);
        settings.enhanced=true;settings.aspect=wide?FZERO_ASPECT_16_9:FZERO_ASPECT_STOCK;
        FzeroViewport v=FzeroCalculateViewport(&settings,wide?1280:640,wide?720:480);
        unsigned scale=wide?2:1,width=v.width*scale,height=224*scale;
        size_t count=(size_t)width*height;
        uint32_t *picture=calloc(count*2,sizeof(*picture)),*notice=calloc(count,sizeof(*notice));
        if(!picture || !notice){free(picture);free(notice);return false;}
        bool ok=FzeroLocalDraw(picture,count*2,v,1,scale);
        FzeroUiLapNotice(FzeroLocalUi(),(FzeroUiCanvas){notice,(int)width,(int)height,scale},
            r->completed_laps,age,r->lap_boost!=0);
        unsigned visible=0;
        for(size_t i=0;ok && i<count;++i)if(notice[i]) {
          ++visible;if(picture[player*count+i]!=notice[i])ok=false;
        }
        if(age==0 || (final && age==32))ok=ok && visible>100;
        if(final && (age==128 || age==160))ok=ok && (age==128?visible>100:!visible);
        if(ok && (age==0 || (final && age==22))) {
          char name[96];snprintf(name,sizeof(name),"lap-p%u-completed%u-age%u-%u-%ux",player+1,r->completed_laps,ages[n],v.width,scale);
          ok=feedback_picture(folder,name,picture,width,height*2);
        }
        free(picture);free(notice);if(!ok)return false;
      }
      if(FzeroVersusHash(FzeroLocalSession())!=hash)return false;
      seen[player][final]|=1u<<n;
      fprintf(stderr,"lap-cues: PASS frame=%ld player=%u completed=%u age=%u boost=%u native/wide2x immutable\n",
          frame,player+1,r->completed_laps,ages[n],r->lap_boost);
    }
  }
  return true;
}
