// Only preference members referenced by this category; save and seed identities are excluded.
static void reset_category(int page){Settings d;settings_defaults(&d);switch(page){
case 0:
memcpy(&g_settings.antialias,&d.antialias,sizeof d.antialias);
memcpy(&g_settings.aspect,&d.aspect,sizeof d.aspect);
memcpy(&g_settings.blur_removal,&d.blur_removal,sizeof d.blur_removal);
memcpy(&g_settings.fullscreen,&d.fullscreen,sizeof d.fullscreen);
memcpy(&g_settings.hide_hero_icon,&d.hide_hero_icon,sizeof d.hide_hero_icon);
memcpy(&g_settings.res_auto,&d.res_auto,sizeof d.res_auto);
memcpy(&g_settings.res_mode,&d.res_mode,sizeof d.res_mode);
memcpy(&g_settings.show_fps,&d.show_fps,sizeof d.show_fps);
memcpy(&g_settings.upscaler,&d.upscaler,sizeof d.upscaler);
memcpy(&g_settings.upscaler_auto,&d.upscaler_auto,sizeof d.upscaler_auto);
break;
case 1:
memcpy(&g_settings.movie_audio,&d.movie_audio,sizeof d.movie_audio);
memcpy(&g_settings.reverb,&d.reverb,sizeof d.reverb);
memcpy(&g_settings.volume,&d.volume,sizeof d.volume);
break;
case 7:
memcpy(&g_settings.enemy_colours,&d.enemy_colours,sizeof d.enemy_colours);
memcpy(&g_settings.grass,&d.grass,sizeof d.grass);
memcpy(&g_settings.hair,&d.hair,sizeof d.hair);
memcpy(&g_settings.hearts,&d.hearts,sizeof d.hearts);
memcpy(&g_settings.laonline_username,&d.laonline_username,sizeof d.laonline_username);
memcpy(&g_settings.leaves,&d.leaves,sizeof d.leaves);
memcpy(&g_settings.sand,&d.sand,sizeof d.sand);
memcpy(&g_settings.screen_filter,&d.screen_filter,sizeof d.screen_filter);
memcpy(&g_settings.shield,&d.shield,sizeof d.shield);
memcpy(&g_settings.skin,&d.skin,sizeof d.skin);
memcpy(&g_settings.tunic,&d.tunic,sizeof d.tunic);
break;
case 10:
memcpy(&g_settings.laonline_always_feed,&d.laonline_always_feed,sizeof d.laonline_always_feed);
memcpy(&g_settings.laonline_bubbles,&d.laonline_bubbles,sizeof d.laonline_bubbles);
memcpy(&g_settings.laonline_chat,&d.laonline_chat,sizeof d.laonline_chat);
memcpy(&g_settings.laonline_chat_seconds,&d.laonline_chat_seconds,sizeof d.laonline_chat_seconds);
memcpy(&g_settings.laonline_collection_counters,&d.laonline_collection_counters,sizeof d.laonline_collection_counters);
memcpy(&g_settings.laonline_death_notices,&d.laonline_death_notices,sizeof d.laonline_death_notices);
memcpy(&g_settings.laonline_death_style,&d.laonline_death_style,sizeof d.laonline_death_style);
memcpy(&g_settings.laonline_dungeon_minimap,&d.laonline_dungeon_minimap,sizeof d.laonline_dungeon_minimap);
memcpy(&g_settings.laonline_feed,&d.laonline_feed,sizeof d.laonline_feed);
memcpy(&g_settings.laonline_feed_seconds,&d.laonline_feed_seconds,sizeof d.laonline_feed_seconds);
memcpy(&g_settings.laonline_hearts,&d.laonline_hearts,sizeof d.laonline_hearts);
memcpy(&g_settings.laonline_indicators,&d.laonline_indicators,sizeof d.laonline_indicators);
memcpy(&g_settings.laonline_item_tracker,&d.laonline_item_tracker,sizeof d.laonline_item_tracker);
memcpy(&g_settings.laonline_markers,&d.laonline_markers,sizeof d.laonline_markers);
memcpy(&g_settings.laonline_minimap,&d.laonline_minimap,sizeof d.laonline_minimap);
memcpy(&g_settings.laonline_minimap_size,&d.laonline_minimap_size,sizeof d.laonline_minimap_size);
memcpy(&g_settings.laonline_pickup_notifications,&d.laonline_pickup_notifications,sizeof d.laonline_pickup_notifications);
memcpy(&g_settings.laonline_shared_items,&d.laonline_shared_items,sizeof d.laonline_shared_items);
g_settings.laonline_sync_story=d.laonline_sync_story;g_settings.laonline_sync_supplies=d.laonline_sync_supplies;g_settings.laonline_sync_keys=d.laonline_sync_keys;g_settings.laonline_sync_world=d.laonline_sync_world;
g_settings.laonline_sync_upgrades=d.laonline_sync_upgrades;g_settings.laonline_sync_souls=d.laonline_sync_souls;
memcpy(&g_settings.laonline_tracker_opacity,&d.laonline_tracker_opacity,sizeof d.laonline_tracker_opacity);
memcpy(&g_settings.laonline_tracker_size,&d.laonline_tracker_size,sizeof d.laonline_tracker_size);
memcpy(&g_settings.laonline_tracker_x,&d.laonline_tracker_x,sizeof d.laonline_tracker_x);
memcpy(&g_settings.laonline_tracker_y,&d.laonline_tracker_y,sizeof d.laonline_tracker_y);
memcpy(&g_settings.laonline_typing,&d.laonline_typing,sizeof d.laonline_typing);
memcpy(&g_settings.laonline_username,&d.laonline_username,sizeof d.laonline_username);
break;
case 8:
memcpy(&g_settings.dpad_shortcuts,&d.dpad_shortcuts,sizeof d.dpad_shortcuts);
memcpy(&g_settings.dpad_hud,&d.dpad_hud,sizeof d.dpad_hud);
memcpy(&g_settings.dpad_up,&d.dpad_up,sizeof d.dpad_up);
memcpy(&g_settings.dpad_down,&d.dpad_down,sizeof d.dpad_down);
memcpy(&g_settings.dpad_left,&d.dpad_left,sizeof d.dpad_left);
memcpy(&g_settings.dpad_right,&d.dpad_right,sizeof d.dpad_right);
memcpy(&g_settings.dpad_size,&d.dpad_size,sizeof d.dpad_size);
memcpy(&g_settings.dpad_opacity,&d.dpad_opacity,sizeof d.dpad_opacity);
memcpy(&g_settings.dpad_x,&d.dpad_x,sizeof d.dpad_x);
memcpy(&g_settings.dpad_y,&d.dpad_y,sizeof d.dpad_y);
memcpy(&g_settings.bg_input,&d.bg_input,sizeof d.bg_input);
memcpy(&g_settings.controller_light,&d.controller_light,sizeof d.controller_light);
memcpy(&g_settings.dpad_move,&d.dpad_move,sizeof d.dpad_move);
memcpy(&g_settings.menu_confirm,&d.menu_confirm,sizeof d.menu_confirm);
memcpy(&g_settings.move_360,&d.move_360,sizeof d.move_360);
memcpy(&g_settings.prompts,&d.prompts,sizeof d.prompts);
memcpy(&g_settings.rumble,&d.rumble,sizeof d.rumble);
memcpy(&g_settings.rumble_strength,&d.rumble_strength,sizeof d.rumble_strength);
break;
case 6:
memcpy(&g_settings.cheat_hookshot,&d.cheat_hookshot,sizeof d.cheat_hookshot);
memcpy(&g_settings.diff_damage,&d.diff_damage,sizeof d.diff_damage);
memcpy(&g_settings.diff_hero,&d.diff_hero,sizeof d.diff_hero);
memcpy(&g_settings.diff_ohko,&d.diff_ohko,sizeof d.diff_ohko);
memcpy(&g_settings.language,&d.language,sizeof d.language);
memcpy(&g_settings.livesplit,&d.livesplit,sizeof d.livesplit);
memcpy(&g_settings.livesplit_host,&d.livesplit_host,sizeof d.livesplit_host);
memcpy(&g_settings.mirror_mode,&d.mirror_mode,sizeof d.mirror_mode);
memcpy(&g_settings.no_buff_drops,&d.no_buff_drops,sizeof d.no_buff_drops);
memcpy(&g_settings.timer,&d.timer,sizeof d.timer);
memcpy(&g_settings.timer_pos,&d.timer_pos,sizeof d.timer_pos);
memcpy(&g_settings.walk_speed,&d.walk_speed,sizeof d.walk_speed);
memcpy(&g_settings.walk_speed_dungeons,&d.walk_speed_dungeons,sizeof d.walk_speed_dungeons);
break;
case 5:
memcpy(&g_settings.cheat_ammo,&d.cheat_ammo,sizeof d.cheat_ammo);
memcpy(&g_settings.cheat_hearts,&d.cheat_hearts,sizeof d.cheat_hearts);
memcpy(&g_settings.cheat_keep_bowwow,&d.cheat_keep_bowwow,sizeof d.cheat_keep_bowwow);
memcpy(&g_settings.cheat_rupees,&d.cheat_rupees,sizeof d.cheat_rupees);
break;
case 3:memcpy(g_settings.rando_options,d.rando_options,sizeof d.rando_options);rando_ui_settings_reloaded();break;
default:break;}}
