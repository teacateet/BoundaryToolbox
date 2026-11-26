#include "SparrowReticle.hpp"
#include "MarkedHeadRadar.hpp"
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
using namespace SDK;
namespace Radar=BrtHeadRadar;
using Node=UUMG_MinimapNode_C;
using Map=UUMG_HelmetHUD_MiniMap_C;
int passed=0, failed=0;
void check(const char* group,bool value) {
    std::printf("%s %s\n",value?"PASS":"FAIL",group);
    if(value)++passed;else ++failed;
}
struct Seen {
    UTexture2D texture; UPBMarkableComponent key; UPBIndicator head;
    Seen(APlayerController& pc,FVector at) {
        key.MiniMapTexture=&texture;head.RelatedMarkable=&key;head.WorldLocation=at;
        head.owner=&pc;head.viewport=true;
    }
};
struct Scene {
    APlayerController player,otherPlayer; APBCharacter pawn,otherPawn;
    UCanvasPanel panel,elsewhere; Map hud; GameMode mode; APBGameState state; UPBMarkerManager marks;
    Seen first{player,{100,0,0}},second{player,{-150,0,0}}; Radar::View view;
    Scene() {
        UWidgetBlueprintLibrary::made.clear();UWidgetBlueprintLibrary::available.clear();
        player.pawn=&pawn;pawn.controller=&player;otherPlayer.pawn=&otherPawn;otherPawn.controller=&otherPlayer;
        hud.owner=&player;hud.viewport=true;hud.OnwerCharacter=&pawn;hud.MiniMapNodeContainer=&panel;
        panel.parent=&hud;state.GameModeClass.mode=&mode;UGameplayStatics::state=&state;
        USubsystemBlueprintLibrary::manager=&marks;UWidgetBlueprintLibrary::available.push_back(&hud);
        marks.IndicatorMap.Put(&first.key,&first.head);
    }
    FPBMiniMapIconInfo icon(const Seen& target) const {
        FPBMiniMapIconInfo value;
        value.bMarked=true;value.Texture=target.key.MiniMapTexture;value.WorldLocation=target.head.WorldLocation;
        value.position=hud.Get2DPositionByLocation(value.WorldLocation);
        return value;
    }
    void pass() {
#ifndef RADAR_BASELINE
        view.Pass(&pawn,&player,&hud,&marks);
#endif
    }
    int added() const {
        int count=0;
        for(const auto& w:UWidgetBlueprintLibrary::made)if(w->valid && w->parent==&panel && w->visible && w->opacity>0)++count;
        return count;
    }
    Node* only() const {
        if(added()!=1)return nullptr;
        for(const auto& dot:UWidgetBlueprintLibrary::made)if(dot->valid && dot->parent==&panel)return dot.get();
        return nullptr;
    }
    void native(Node& node) {
        node.parent=&panel;node.owner=&player;node.MiniMapIconInfo=icon(first);hud.DrewMap.push_back(&node);
    }
    void event(APBCharacter* actor,std::string_view name) {
#ifndef RADAR_BASELINE
        Radar::OnCharacter(actor,name);
#endif
    }
};
void visibleAndNative() {
    Scene s;s.pass();
    check("visible_head_empty_native_tables",s.added()==1);
    auto* original=s.only();
    if(original) {auto value=original->MiniMapIconInfo;check("project_from_visible_head",value.Texture==&s.first.texture && value.position.X==0.55f);}
    else check("project_from_visible_head",false);
    s.marks.VisionMarkedMap.Put(&s.first.key,&s.first.head);s.marks.ForceMarkedMap.Put(&s.first.key,&s.first.head);
    s.pass();check("same_target_three_lists_one_node",s.added()==1 && s.only()==original && original && original->updates==2);
    Node stock;s.native(stock);s.pass();
    check("stock_takes_over_and_stays_attached",s.added()==0 && stock.parent==&s.panel);
    s.hud.DrewMap.clear();s.pass();check("stock_released_fallback_returns",s.added()==1);
}
void poolAndVisibility() {
    Scene s;Node n;s.native(n);n.parent=&s.elsewhere;s.pass();
    check("pooled_node_does_not_cover",s.added()==1 && n.parent==&s.elsewhere);
    n.parent=&s.panel;s.hud.NotUsedWidgetArray.push_back(&n);s.pass();
    check("stock_listed_in_pool_does_not_cover",s.added()==1);
    s.hud.NotUsedWidgetArray.clear();n.visible=false;s.pass();check("hidden_stock_does_not_cover",s.added()==1);
    n.visible=true;n.opacity=0;s.pass();check("transparent_stock_does_not_cover",s.added()==1);
    n.opacity=1;n.valid=false;s.pass();check("invalid_stock_does_not_cover",s.added()==1);
    n.valid=true;n.owner=&s.otherPlayer;s.pass();check("foreign_viewer_stock_does_not_cover",s.added()==1);
    if(auto* old=s.only()){old->serial++;old->parent=&s.elsewhere;}
    s.pass();check("recycled_widget_gets_new_generation",s.added()==1 && UWidgetBlueprintLibrary::made.size()>=2);
}
void rejectAndRecover() {
    Scene s;s.pass();s.first.key.clear=false;s.pass();check("occluded_target_retracted",s.added()==0);
    s.first.key.clear=true;s.pass();check("visible_again_restored",s.added()==1);
    s.first.head.MarkedStatus=EPBMarkedStatus::None;s.pass();check("expired_mark_retracted",s.added()==0);
    s.first.head.MarkedStatus=EPBMarkedStatus::SensorMark;s.first.head.WorldLocation={0,0,1001};s.pass();check("vertical_range_applies",s.added()==0);
    s.first.head.WorldLocation={1000,0,0};s.pass();check("range_boundary_inclusive",s.added()==1);
    s.first.head.owner=&s.otherPlayer;s.pass();check("foreign_head_retracted",s.added()==0);
    s.first.head.owner=&s.player;s.first.head.Camp=EPBCamp::Ally;s.pass();check("friendly_head_excluded",s.added()==0);
    s.first.head.Camp=EPBCamp::Enemy;s.first.head.bIsDying=true;s.pass();check("dying_mark_excluded",s.added()==0);
    s.first.head.bIsDying=false;s.first.head.parent=&s.elsewhere;s.elsewhere.opacity=0;s.pass();check("transparent_head_parent_excluded",s.added()==0);
    s.elsewhere.opacity=1;s.first.head.size.X=0;s.pass();check("empty_geometry_excluded",s.added()==0);
    s.first.head.size.X=50;s.first.head.Status=EPBIndicatorStatus::Outside;s.pass();check("offscreen_head_excluded",s.added()==0);
}
void cacheAndTargets() {
    Scene s;auto old=s.icon(s.first);old.WorldLocation={-400,0,0};old.position=s.hud.Get2DPositionByLocation(old.WorldLocation);
    s.hud.LastCaptureInfoNodeMap.Put(&s.first.key,old);s.pass();
    check("old_capture_tracks_current_visible_location",s.only() && s.only()->MiniMapIconInfo.position.X==0.55f);
    s.hud.LastCaptureInfoNodeMap.Reset();s.marks.VisionMarkedMap.Put(&s.second.key,&s.second.head);
    s.marks.ForceMarkedMap.Put(&s.second.key,&s.second.head);s.pass();check("two_distinct_targets_draw_once_each",s.added()==2);
    s.marks.IndicatorMap.Reset();s.pass();check("first_removed_second_kept",s.added()==1 && s.only()->MiniMapIconInfo.Texture==&s.second.texture);
    s.marks.VisionMarkedMap.Reset();s.marks.ForceMarkedMap.Reset();s.pass();check("all_marks_removed",s.added()==0);
}
void staleMapRecord() {
    Scene s;
    auto stale=s.icon(s.first);
    stale.bMarked=false; stale.Texture=nullptr;
    stale.position={2,2};
    s.hud.MapNodeInfo.Put(&s.first.key,stale);
    s.pass();
    check("visible_mark_supersedes_unmarked_untextured_cache",s.only() && s.only()->MiniMapIconInfo.bMarked &&
          s.only()->MiniMapIconInfo.Texture==&s.first.texture && s.only()->MiniMapIconInfo.position.X==0.55f);
    s.hud.MapNodeInfo.Reset();s.hud.LastCaptureInfoNodeMap.Put(&s.first.key,stale);
    s.pass();check("previous_unmarked_capture_cannot_veto_visible_head",s.added()==1);
    s.hud.LastCaptureInfoNodeMap.Reset();stale=s.icon(s.first);stale.position={2,2};s.hud.MapNodeInfo.Put(&s.first.key,stale);
    s.pass();check("visible_mark_uses_current_head_projection",s.only() && s.only()->MiniMapIconInfo.position.X==0.55f);
    s.hud.MapNodeInfo.Reset();stale.bIsLocal=true;s.hud.MapNodeInfo.Put(&s.first.key,stale);
    s.pass();check("local_player_cache_still_excluded",s.added()==0);
    s.hud.MapNodeInfo.Reset();s.first.key.MiniMapTexture=nullptr;s.pass();
    check("no_valid_icon_keeps_map_clear",s.added()==0);
}
void lifecycle(bool enabled) {
    Scene s;
    s.hud.viewport=false;
    Map stale;
    stale.owner=&s.player;stale.MiniMapNodeContainer=&s.panel;
    UWidgetBlueprintLibrary::available.insert(UWidgetBlueprintLibrary::available.begin(),&stale);
    auto tick=[&](){std::this_thread::sleep_for(std::chrono::milliseconds(135));s.event(&s.pawn,"Actor.ReceiveTick");};
    tick();check(enabled?"supported_pve_mode":"launch_flag_absent",s.added()==(enabled?1:0));
    if(!enabled)return;
    s.event(&s.otherPawn,"NotifyBeKilled");check("other_player_death_preserves_owner",s.added()==1);
    s.event(&s.pawn,"NotifyBeKilled");check("owner_death_clears_immediately",s.added()==0);
    tick();check("death_before_state_replication_stays_cleared",s.added()==0);
    s.pawn.alive=false;s.pawn.killed=true;tick();
    s.pawn.alive=true;s.pawn.killed=false;tick();check("confirmed_respawn_restores",s.added()==1);
    s.pawn.radar=false;tick();check("signal_lost_removes_marker",s.added()==0);
    s.pawn.radar=true;tick();check("signal_recovers_before_mode_change",s.added()==1);
    s.mode.name="BP_PBGameMode_PvP_C";tick();check("mode_change_removes_marker",s.added()==0);
    s.mode.name="BP_PBGameMode_Rush_PVE_Hard_C";tick();check("hard_pve_mode_resumes",s.added()==1);
    s.player.local=false;s.event(&s.pawn,"ReceiveTick");check("control_lost_clears",s.added()==0);
    s.player.local=true;tick();s.event(&s.pawn,"ReceiveEndPlay");check("endplay_clears",s.added()==0);
}
void replicatedMarks() {
    Scene s;
    s.marks.IndicatorMap.Reset();
    APBPlayerState me, other;
    other.team=1;
    s.player.PlayerState=&me;
    s.otherPawn.TeamID=1;
    s.otherPawn.at={100,0,0};
    UPBDynamicMarkableComponent marked;
    marked.owner=&s.otherPawn;
    marked.MiniMapTexture=&s.first.texture;
    marked.bMarked=true;
    marked.MarkCounterArray.push_back(&other);
    s.marks.MarkedArray.push_back(&marked);
    s.pass(); check("opponent_private_mark_hidden",s.added()==0);
    marked.MarkCounterArray.push_back(&me);
    s.pass(); check("own_visible_mark_without_head",s.added()==1);
    s.player.inside=false;
    s.pass(); check("offscreen_replicated_mark_removed",s.added()==0);
    s.player.inside=true; marked.bMarked=false;
    s.pass(); check("expired_replicated_mark_removed",s.added()==0);
}

// 零售 SupplyPoint_BP 的 PBMarkable 是静态组件：原版开启 bLocalMarked，
// 使用 InteractiveObj 和独立的 AmmoCase 纹理，无须先侦察或对准补给点。
void supplyMarkers() {
    Scene s; s.marks.IndicatorMap.Reset();
    APBSupplyPoint box; UTexture2D ammo; UPBMarkableComponent key;
    key.owner=&box; key.bLocalMarked=true; key.MarkableType=EPBMarkableType::InteractiveObj;
    key.MiniMapTexture=&ammo; key.at={200,0,0};
    s.marks.LocalMarkedArray.push_back(&key); s.player.inside=false; key.clear=false;
    s.pass();
    auto* dot=s.only();
    check("static_supply_without_enemy_or_head_uses_original_ammo_brush",dot && dot->MiniMapIconInfo.Texture==&ammo &&
          dot->MiniMapIconInfo.MarkableType==EPBMarkableType::InteractiveObj && dot->MiniMapIconInfo.Camp==EPBCamp::Neutral &&
          dot->MiniMapIconInfo.position.X==0.6f);
    Node stock;stock.parent=&s.panel;stock.owner=&s.player;stock.MiniMapIconInfo.Texture=&ammo;
    stock.MiniMapIconInfo.MarkableType=EPBMarkableType::InteractiveObj;stock.MiniMapIconInfo.WorldLocation=key.at;
    stock.MiniMapIconInfo.bMarked=false;stock.MiniMapIconInfo.position={0.59f,0.5f};
    s.hud.DrewMap.push_back(&stock);s.pass();
    check("visible_native_supply_takes_over_even_between_projection_frames",s.added()==0 && stock.parent==&s.panel);
    s.hud.NotUsedWidgetArray.push_back(&stock);s.pass();check("pooled_native_supply_does_not_hide_ammo_icon",s.added()==1);
    s.hud.NotUsedWidgetArray.clear();s.hud.DrewMap.clear();
    key.at={0,0,1001};s.pass();check("supply_outside_current_radar_range_retracts",s.added()==0);
    key.at={200,0,0};s.hud.IconPositionUVClamp_U={0,0.55f};s.pass();
    check("supply_outside_native_projection_clamp_remains_absent",s.added()==0);
    s.hud.IconPositionUVClamp_U={0,1};s.pass();check("supply_reappears_on_reentering_hud_range",s.added()==1);
    key.bLocalMarked=false;s.pass();check("private_or_disabled_static_marker_hidden",s.added()==0);
    key.bLocalMarked=true;key.owner=&s.otherPawn;s.pass();check("unrelated_interactive_object_excluded",s.added()==0);
    key.owner=&box;box.bHidden=true;s.pass();check("hidden_supply_actor_excluded",s.added()==0);
    box.bHidden=false;APBGameState previous;box.worldState=&previous;s.pass();check("supply_from_old_world_excluded",s.added()==0);
    box.worldState=nullptr;key.MiniMapTexture=nullptr;s.pass();check("missing_native_ammo_texture_does_not_make_placeholder",s.added()==0);
    key.MiniMapTexture=&ammo;s.pass();check("valid_public_supply_restored",s.added()==1);
    s.marks.LocalMarkedArray.clear();s.pass();check("unregistered_supply_retracted",s.added()==0);
    UPBIndicator head;head.owner=&s.otherPlayer;head.viewport=true;head.RelatedMarkable=&key;head.Camp=EPBCamp::Neutral;
    s.marks.IndicatorMap.Put(&key,&head);s.pass();check("foreign_supply_hud_ignored",s.added()==0);
    head.owner=&s.player;s.pass();check("local_visible_supply_indicator_without_registry_row",s.added()==1);
    s.marks.LocalMarkedArray.push_back(&key);s.pass();check("same_supply_in_hud_and_registry_drawn_once",s.added()==1);
    s.marks.LocalMarkedArray.clear();head.opacity=0;s.pass();check("no_public_source_left_retracts_supply",s.added()==0);
}

void supplyLifecycle(bool enabled) {
    Scene s;s.marks.IndicatorMap.Reset();APBSupplyPoint box;UPBMarkableComponent key;
    key.owner=&box;key.MiniMapTexture=&s.first.texture;key.bLocalMarked=true;key.MarkableType=EPBMarkableType::InteractiveObj;
    key.at={100,0,0};s.marks.LocalMarkedArray.push_back(&key);
    auto tick=[&](){std::this_thread::sleep_for(std::chrono::milliseconds(135));s.event(&s.pawn,"Actor.ReceiveTick");};
    tick();check(enabled?"live_pve_supply_visible":"supply_absent_without_local_pve_flag",s.added()==(enabled?1:0));
    if(enabled) {
        s.pawn.radar=false;tick();check("loss_of_native_radar_signal_retracts_supply",s.added()==0);
        s.pawn.radar=true;tick();check("native_radar_signal_restores_one_supply",s.added()==1);
        s.event(&s.pawn,"NotifyBeKilled");check("death_retracts_supply_immediately",s.added()==0);
        s.pawn.alive=false;s.pawn.killed=true;tick();s.pawn.alive=true;s.pawn.killed=false;tick();
        check("respawn_reattaches_one_supply",s.added()==1);
        s.mode.name="BP_PBGameMode_PvP_C";tick();check("supply_absent_after_leaving_supported_pve",s.added()==0);
    }
    s.event(&s.pawn,"ReceiveEndPlay");
}
int main(int argc,char** argv) {
    const bool enabled=argc==1;
    UKismetSystemLibrary::requested=enabled;
    if(enabled){visibleAndNative();poolAndVisibility();rejectAndRecover();cacheAndTargets();staleMapRecord();replicatedMarks();supplyMarkers();}
    lifecycle(enabled);
    supplyLifecycle(enabled);
    std::printf("SUMMARY passed=%d failed=%d\n",passed,failed);
#ifdef RADAR_BASELINE
    return 0;
#else
    return failed?1:0;
#endif
}
