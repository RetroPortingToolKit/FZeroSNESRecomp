#include "fzero_netplay_mods.h"
#include <stdio.h>
#include <string.h>

static const RecompLauncherCModProvider *source;
static RecompLauncherCModProvider provider;
static bool online;
static const char *error;
#define COPY(dst,src) snprintf(dst,sizeof(dst),"%s",src)
void FzeroNetplayModsMode(bool active) {online=active;error=NULL;}
static int count(void *ctx) {return online?3:source->feature_count(ctx);}
static int packages(void *ctx) {return online?3:source->package_count(ctx);}
static int presets(void *ctx) {return online?0:source->preset_count?source->preset_count(ctx):0;}
static int feature(void *ctx,int index,RecompLauncherCModFeature *out) {
  if(!online)return source->feature_get(ctx,index,out);
  if(!out || index<0 || index>=3)return 0;
  memset(out,0,sizeof(*out));
  if(index==0) {
    COPY(out->id,"versus-rules");COPY(out->package_id,"fzero-online-versus");
    COPY(out->name,"Online VS rules and machines");COPY(out->package_name,out->name);
    COPY(out->package_version,"1");COPY(out->group,"Network gameplay");out->enabled=1;out->hidden=1;
    COPY(out->status,"Required for netplay");
    COPY(out->description,"Both players use the same fixed VS rules and all twelve CGP machines, each with its authored artwork, handling and energy boost. Single-player car and rule switches do not change VS. Start opens a menu while the race continues. These rules cannot be disabled in a lobby.");
    return 1;
  }
  const char *wanted=index==1?"track-pack-loader":"bs-tracks";
  for(int i=0;i<source->feature_count(ctx);++i) {
    if(source->feature_get(ctx,i,out) && !strcmp(out->package_id,wanted)) {
      COPY(out->group,"Network course catalog");out->option_count=0;out->hidden=0;
      COPY(out->status,out->enabled?"Enabled; must match both players":"Disabled; must match both players");
      COPY(out->description,index==1?
          "Installed track packs are available in online VS. Both players must have the same enabled course catalog; it is verified before the match starts. Change installed packs or the loader in the main Mods screen before entering a lobby. Music and graphics settings stay local and are not lobby gameplay mods.":
          "The original BS course set is part of the shared catalog when enabled. Both players must match. Change this in the main Mods screen before entering a lobby. The imported track loader includes corrected BS courses and is mutually exclusive with this legacy set.");
      return 1;
    }
  }
  return 0;
}
static int package(void *ctx,int index,RecompLauncherCModPackage *out) {
  if(!online)return source->package_get(ctx,index,out);
  RecompLauncherCModFeature f;if(!out || !feature(ctx,index,&f))return 0;
  memset(out,0,sizeof(*out));COPY(out->id,f.package_id);COPY(out->name,f.name);
  COPY(out->version,f.package_version);COPY(out->description,f.description);out->enabled=f.enabled;return 1;
}
static int blocked(void) {error="VS rules are fixed. Change the shared course catalog in the main Mods screen before entering a lobby; both players must match.";return 0;}
static int enable(void *ctx,const char *p,const char *f,int value) {
  return online?blocked():source->feature_enable(ctx,p,f,value);
}
static int set_enabled(void *ctx,const char *p,int value) {return online?blocked():source->set_enabled(ctx,p,value);}
static int commit(void *ctx,const char *rom) {
  (void)ctx;(void)rom;
  /* The runtime verifies the fixed rules/build and complete active catalog.
   * Do not rewrite offline selections or claim to transfer content. */
  return 1;
}
static const char *last_error(void *ctx) {return error?error:source->last_error?source->last_error(ctx):"";}
const RecompLauncherCModProvider *FzeroNetplayMods(const RecompLauncherCModProvider *offline) {
  if(offline==&provider)return &provider;
  source=offline;provider=*offline;provider.hide_hidden_features=1;provider.feature_count=count;provider.feature_get=feature;
  provider.package_count=packages;provider.package_get=package;provider.preset_count=presets;
  provider.feature_enable=enable;provider.set_enabled=set_enabled;provider.last_error=last_error;
  provider.commit_netplay=commit;return &provider;
}
