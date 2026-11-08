#pragma once
/* Explicit presentation fixture: place completed poses without simulating
 * them, then restore the complete race before ordinary driving continues. */
static bool rear_warning_check(void) {
  static bool done;const char *folder=getenv("FZERO_TEST_REAR_WARNING");
  if(!folder || done)return true;
  FzeroVersus *session=FzeroLocalSession();
  const FzeroVersusSnapshot *read=FzeroVersusCompleted(session);
  if(!read || read->phase!=FZERO_VS_RACING || read->race_tick<62)return true;
  FzeroVersusSnapshot *race=(FzeroVersusSnapshot *)(void *)read,original=*read;
  uint64_t hash=FzeroVersusHash(session);unsigned players=race->config.player_count;
  const FzeroNativeUi *ui=FzeroLocalUi();if(!ui)return false;
  /* Independent native-loader check: one-plane ROM CHECK masks become OBJ
   * colors0/8, rather than the black color1 of an unexpanded mask. */
  for(unsigned half=0;half<2;++half)for(unsigned part=0;part<4;++part)
    for(unsigned y=0;y<8;++y)for(unsigned x=0;x<8;++x) {
      unsigned id=0xca+half*16+part,color=0;
      for(unsigned b=0;b<4;++b)color|=(g_snes->ppu->vram[0x5000+id*16+y+b/2*8]>>(7-x+b%2*8)&1)<<b;
      if(color!=ui->objects[id][y*8+x])return false;
    }
  unsigned base_x=race->racers[0].actor.x,base_y=race->racers[0].actor.y;
  bool ok=true;
  for(unsigned wide=0;wide<2 && ok;++wide)for(unsigned scale=1;scale<=2 && ok;++scale) {
    unsigned pane=wide?342:256,width=pane*(players>2?2:1),height=448;
    FzeroViewport view={(int)width,((int)width-256)/2,wide?16.0/9:4.0/3,true};
    size_t size=(size_t)width*height*scale*scale;
    uint32_t *frame=calloc(size,4),*mark=calloc((size_t)pane*224*scale*scale,4);
    if(!frame || !mark) {free(frame);free(mark);ok=false;break;}
    for(unsigned state=0;state<2 && ok;++state) {
      for(unsigned i=0;i<players;++i) {
        race->racers[i].actor.x=(uint16_t)base_x;
        race->racers[i].actor.y=(uint16_t)((base_y+(state?0:i*48))&4095);
        race->racers[i].actor.facing=0;
      }
      FzeroLocalResetViews();ok=FzeroLocalDraw(frame,size,view,1,scale);
      for(unsigned player=0;player<players && ok;++player) {
        FzeroRearWarning warning=FzeroVersusRearWarning(race,player,ui->tables);
        bool expected=!state && player+1<players;
        if(warning.visible!=expected) {ok=false;break;}
        unsigned ox=players>2?player%2*pane:0,oy=players>2?player/2*224:player*224;
        memset(mark,0,(size_t)pane*224*scale*scale*4);
        FzeroUiRearWarning(ui,(FzeroUiCanvas){mark,(int)(pane*scale),(int)(224*scale),scale},128,36,race->tick);
        unsigned count=0,matched=0;
        for(unsigned y=0;y<224*scale;++y)for(unsigned x=0;x<pane*scale;++x) {
          uint32_t pixel=mark[(size_t)y*pane*scale+x];if(!pixel)continue;
          ++count;matched+=frame[((size_t)oy*scale+y)*width*scale+ox*scale+x]==pixel;
        }
        if(!count || (expected?matched!=count:matched>=count/2))ok=false;
      }
      if(players==3)for(unsigned y=224*scale;y<448*scale && ok;++y)
        for(unsigned x=pane*scale;x<width*scale;++x)if(frame[(size_t)y*width*scale+x]) {ok=false;break;}
      if(ok && scale==1) {
        char name[96];snprintf(name,sizeof(name),"rear-%up-%u-%s",players,pane,state?"clear":"following");
        ok=feedback_picture(folder,name,frame,width,height);
      }
    }
    free(frame);free(mark);
  }
  *race=original;FzeroLocalResetViews();
  ok=ok && FzeroVersusHash(session)==hash;
  if(ok)fprintf(stderr,"rear-warning: PASS %uP native/wide1x/2x independent panes, last-racer clear, no stale marks, immutable race\n",players);
  done=ok;return ok;
}
