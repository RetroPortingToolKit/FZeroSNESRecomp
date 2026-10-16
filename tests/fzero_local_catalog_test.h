#pragma once
/* Explicit headless, owner-authorized teleport qualification. Map resources,
 * progress, laps, results and snapshots are real; this is NOT a driving bot. */
static const char *catalog_output;
static unsigned catalog_course,catalog_end,catalog_checked,catalog_passed;
static unsigned catalog_finish_shots;
static FILE *catalog_report;
static bool catalog_snapshot(bool rematch) {
  FzeroVersus *session=FzeroLocalSession();uint64_t hash=FzeroVersusHash(session);
  size_t size=RtlSaveSnapshotToMemory(NULL,0);uint8_t *data=size?malloc(size):NULL;
  if(!data || RtlSaveSnapshotToMemory(data,size)!=size) {free(data);return false;}
  if(rematch) {
    if(!FzeroVersusRematch(session)) {free(data);return false;}
    FzeroVersusReady(session,0,true);FzeroVersusReady(session,1,true);
    for(unsigned i=0;i<FZERO_VERSUS_COUNTDOWN;++i)
      if(!FzeroVersusTick(session,(const uint16_t[2]){0,0})) {free(data);return false;}
  }
  bool ok=RtlLoadSnapshotFromMemory(data,size) && FzeroVersusHash(FzeroLocalSession())==hash;
  free(data);return ok;
}
static bool catalog_image(unsigned id,const char *tag) {
  static uint32_t frame[256*448],again[256*448],hd[512*896];
  FzeroViewport viewport={256,0,4.0/3.0,false};
  FzeroLocalPublish();
  if(!FzeroLocalDraw(frame,256*448,viewport,1,1) ||
      !FzeroLocalDraw(hd,512*896,viewport,1,2) ||
      !FzeroLocalDraw(again,256*448,viewport,1,1) || memcmp(frame,again,sizeof(frame)))return false;
  char path[2048];snprintf(path,sizeof(path),"%s/course-%03u%s.ppm",catalog_output,id,tag?tag:"");
  FILE *file=fopen(path,"wb");if(!file)return false;fprintf(file,"P6\n256 448\n255\n");
  for(unsigned i=0;i<256*448;++i) {
    const uint8_t rgb[3]={(uint8_t)(frame[i]>>16),(uint8_t)(frame[i]>>8),(uint8_t)frame[i]};
    if(fwrite(rgb,1,3,file)!=3) {fclose(file);return false;}
  }
  return !fclose(file);
}
static bool catalog_init(void) {
  catalog_output=getenv("FZERO_TEST_LOCAL_CATALOG");if(!catalog_output)return true;
  if(!FzeroLocalEnabled())return false;
  catalog_end=FzeroLocalCourseId(FzeroLocalCupCount(),0);
  catalog_course=FzeroLocalConfig().course;
  const char *count=getenv("FZERO_TEST_LOCAL_CATALOG_COUNT");
  if(count && catalog_course+(unsigned)atoi(count)<catalog_end)catalog_end=catalog_course+(unsigned)atoi(count);
  char path[2048];snprintf(path,sizeof(path),"%s/catalog.tsv",catalog_output);
  catalog_report=fopen(path,"wb");if(!catalog_report)return false;
  fputs("id\tcup\torder\tleague\tcourse\tfeatures\tworld_hash\n",catalog_report);
  fprintf(stderr,"catalog-test: %u cups, courses %u..%u, teleport flow only\n",FzeroLocalCupCount(),catalog_course,catalog_end-1);
  return true;
}
/* Called at a frame boundary. 1 completes the run, -1 fails qualification. */
static int catalog_step(void) {
  if(!catalog_output)return 0;
  const FzeroVersusSnapshot *race=FzeroVersusCompleted(FzeroLocalSession());if(!race)return 0;
  const uint8_t *world=FzeroVersusWorld(FzeroLocalSession());
  if(!(catalog_checked&1)) {
    for(unsigned y=0;y<4096;y+=8)for(unsigned x=0;x<8192;x+=8)
      if(FzeroActorSampleSurface(world,(uint16_t)x,(uint16_t)y)!=FzeroActorSampleSurface(g_ram,(uint16_t)x,(uint16_t)y))return -1;
    const FzeroCourse *course=FzeroTracksCurrentCourse();
    if(course && (!world[FZERO_WORLD_RESOURCE] || memcmp(world+FZERO_WORLD_COURSE_HASH,course->hash,32) ||
        memcmp(world+FZERO_WORLD_TERRAIN,course->terrain,0x400) || world[FZERO_WORLD_FEATURES]!=course->required))return -1;
    unsigned cup,order;if(!FzeroLocalCourseLocation(catalog_course,&cup,&order))return -1;
    fprintf(catalog_report,"%u\t%u\t%u\t%s\t%s\t%u\t%016llx\n",catalog_course,cup,order,
        FzeroLocalCupName(cup),FzeroLocalCourseName(catalog_course),world[FZERO_WORLD_FEATURES],
        (unsigned long long)fnv1a_update(UINT64_C(14695981039346656037),world,0x20000));
    fflush(catalog_report);catalog_checked|=1;
  }
  if(!(catalog_checked&2) && race->phase==FZERO_VS_COUNTDOWN && race->tick>=34) {
    if(!catalog_snapshot(false))return -1;catalog_checked|=2;
  }
  race=FzeroVersusCompleted(FzeroLocalSession());
  if(!(catalog_checked&4) && race->phase==FZERO_VS_RACING && race->race_tick>=30) {
    if(!catalog_snapshot(false) || !catalog_image(catalog_course,NULL))return -1;catalog_checked|=4;
  }
  /* A snapshot restore replaces the session: never retain its old pointer. */
  race=FzeroVersusCompleted(FzeroLocalSession());
  if(getenv("FZERO_TEST_LOCAL_FINISH_IMAGES") && race->racers[0].status==FZERO_VS_FINISHED && race->phase==FZERO_VS_RACING) {
    static const unsigned ages[]={0,20,60,75,100};
    uint64_t age=race->race_tick-race->racers[0].finish_tick;
    for(unsigned shot=0;shot<5;++shot)if(!(catalog_finish_shots&(1u<<shot)) && age>=ages[shot]) {
      if((shot==2 || shot==3) && !catalog_snapshot(false))return -1;
      char tag[64];snprintf(tag,sizeof(tag),"-finish-%03u",ages[shot]);
      uint64_t hash=FzeroVersusHash(FzeroLocalSession());
      if(!catalog_image(catalog_course,tag) || FzeroVersusHash(FzeroLocalSession())!=hash)return -1;
      catalog_finish_shots|=1u<<shot;
    }
  }
  race=FzeroVersusCompleted(FzeroLocalSession());
  if(race->phase!=FZERO_VS_RESULTS)return 0;
  if(!FzeroLocalMenuReady())return 0;
  if(getenv("FZERO_TEST_LOCAL_FINISH_IMAGES") && (catalog_finish_shots!=31 || !catalog_image(catalog_course,"-results")))return -1;
  if(catalog_checked!=7 || race->racers[0].status!=FZERO_VS_FINISHED || race->racers[1].status!=FZERO_VS_FINISHED ||
      race->racers[0].completed_laps!=5 || race->racers[1].completed_laps!=5 || !catalog_snapshot(true))return -1;
  fprintf(stderr,"catalog-test: PASS course=%u %s, two five-lap finishes, arrival/racing/results snapshot, rematch, native/HD image\n",
      catalog_course,FzeroLocalCourseName(catalog_course));++catalog_passed;
  if(++catalog_course==catalog_end) {
    fprintf(stderr,"catalog-test: ALL %u COURSES PASS\n",catalog_passed);fclose(catalog_report);catalog_report=NULL;return 1;
  }
  FzeroLocalShutdown();RtlReset(1);FzeroGameInfo()->session_reset();
  unsigned machines=getenv("FZERO_TEST_LOCAL_CATALOG_ALL_MACHINES")?12:4;
  if(!FzeroLocalConfigure((FzeroVersusConfig){(uint16_t)catalog_course,{(uint8_t)(catalog_course%machines),(uint8_t)((catalog_course+1)%machines)}}))return -1;
  catalog_checked=catalog_finish_shots=0;return 0;
}
