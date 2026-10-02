#include <unity.h>
#include <cmath>
#include "ffb_force_calculator.h"
using namespace SunFFB;
static uint32_t ms, us;
extern "C" uint32_t _millis() { return ms; }
extern "C" uint32_t _micros() { return us; }
extern "C" void _debug_printf(const char*, ...) {}
void setUp() { ms=us=0; }
void tearDown() {}
struct Fixture {
 FFBReportHandler h; FFBDeviceInput input; FFBForceCalculator calc;
 Fixture() { input.reset();input.set_tf_position(0);input.set_tf_speed(0);DeviceControlReportData d{4};h.set_device_control(&d); }
 uint8_t effect(uint8_t type, uint16_t duration=1000, uint8_t axes=X_AXIS_ENABLE, uint16_t angle=0, uint16_t sample=0, uint8_t trigger=USB_NO_TRIGGER_BUTTON) {
  CreateNewEffectReportData c{type};h.create_new_effect(&c);uint8_t id=h.get_block_load_report_data()->effectBlockIndex;
  SetEffectReportData e{};e.effectBlockIndex=id;e.effectType=type;e.duration=duration;e.axisEnable=axes;e.gain=255;e.triggerButton=trigger;e.directions[0]=angle;e.samplePeriod=sample;e.triggerRepeatInterval=USB_DURATION_INFINITE;h.set_effect(&e);return id;
 }
 void start(uint8_t id) { EffectOperationReportData e{id,1,1};h.set_effect_operation(&e); }
 void constant(uint8_t id,int16_t magnitude=4000) {SetConstantForceReportData c{id,magnitude};h.set_constant_force(&c);}
 void condition(uint8_t id,int16_t cp=0,uint16_t dead=0,int16_t coeff=10000,uint16_t positive=10000,uint16_t negative=10000,uint8_t axis=0) {SetConditionReportData c{id,axis,cp,coeff,coeff,positive,negative,dead};h.set_condition(&c);}
 void position(int16_t x,int16_t y=0) {int16_t axes[NUM_AXIS]{};axes[0]=x;
#if NUM_AXIS>=2
 axes[1]=y;
#endif
 us+=10000;input.update_axis(axes);}
 int32_t force(uint8_t axis=0) {int32_t out[NUM_AXIS]{};calc.force_calculator(h,input,out);return out[axis];}
 void control(uint8_t state) {DeviceControlReportData d{state};h.set_device_control(&d);}
};
void condition_center_must_use_descriptor_scale() {
 Fixture f;auto id=f.effect(ET_SPRING);f.condition(id,5000);f.position(16383);f.start(id);
 TEST_ASSERT_INT_WITHIN(1,0,f.force());
}
void condition_deadband_must_use_descriptor_scale() {
 Fixture f;auto id=f.effect(ET_SPRING);f.condition(id,0,5000);f.position(24575);f.start(id);
 TEST_ASSERT_INT_WITHIN(2,-2500,f.force());
}
void saturation_follows_metric_side_even_with_negative_coefficient() {
 Fixture f;auto id=f.effect(ET_SPRING);f.condition(id,0,0,-10000,2000,3000);f.position(32767);f.start(id);
 TEST_ASSERT_EQUAL_INT32(2000,f.force());
}
#if NUM_AXIS == 2
void button_release_must_not_cancel_started_playback() {
 Fixture f;auto id=f.effect(ET_CONSTANT,1000,DIRECTION_ENABLE,9000,0,1);f.constant(id);f.start(id);
 f.input.update_buttons(1);TEST_ASSERT_EQUAL_INT32(-4000,f.force());
 ms=10;f.input.update_buttons(0);TEST_ASSERT_EQUAL_INT32(-4000,f.force());
}
void pid_state_must_identify_second_effect_transition() {
 Fixture f;auto a=f.effect(ET_CONSTANT),b=f.effect(ET_CONSTANT);f.start(a);f.h.pidStateDirty=false;f.start(b);
 TEST_ASSERT_EQUAL_UINT8((b<<1)|1,f.h.get_pid_state_report_data()->effectBlockIndex);
}
void stopped_pid_state_must_keep_the_effect_handle() {
 Fixture f;auto id=f.effect(ET_CONSTANT);f.start(id);EffectOperationReportData op{id,3,1};f.h.set_effect_operation(&op);
 TEST_ASSERT_EQUAL_UINT8(id<<1,f.h.get_pid_state_report_data()->effectBlockIndex);
}
void disabled_actuators_must_still_advance_effect_lifecycle() {
 Fixture f;auto id=f.effect(ET_CONSTANT,10);f.constant(id);f.start(id);f.control(2);ms=20;
 TEST_ASSERT_EQUAL_INT32(0,f.force());
 TEST_ASSERT_EQUAL_UINT8(0,f.h.get_all_effect_blocks()[id-1].state & EFFECT_STATE_PLAYING);
}
void condition_sample_period_must_hold_output_between_samples() {
 Fixture f;auto id=f.effect(ET_SPRING,1000,X_AXIS_ENABLE,0,100);f.condition(id);f.start(id);f.position(0);
 TEST_ASSERT_EQUAL_INT32(0,f.force());ms=50;f.position(32767);TEST_ASSERT_EQUAL_INT32(0,f.force());
}
void zero_sustain_periodic_effect_must_apply_attack_amplitude() {
 Fixture f;auto id=f.effect(ET_SINE,1000,DIRECTION_ENABLE,9000);SetPeriodicReportData p{id,0,0,9000,100};f.h.set_periodic(&p);
 SetEnvelopeReportData e{id,4000,0,100,0};f.h.set_envelope(&e);f.start(id);TEST_ASSERT_EQUAL_INT32(-4000,f.force());
}
void direction_enable_is_not_a_coordinate_format_selector() {
 Fixture f;auto id=f.effect(ET_CONSTANT,1000,X_AXIS_ENABLE|Y_AXIS_ENABLE,0);f.constant(id);f.start(id);
 TEST_ASSERT_EQUAL_INT32(4000,f.force(1));
}
void directed_condition_must_ignore_stale_second_axis_block() {
 Fixture f;auto id=f.effect(ET_SPRING,1000,X_AXIS_ENABLE|Y_AXIS_ENABLE);f.condition(id,0,0,1000);f.condition(id,0,0,7000,10000,10000,1);
 auto e=f.h.get_all_effect_blocks()[id-1].effectData;e.axisEnable=DIRECTION_ENABLE;e.directions[0]=9000;f.h.set_effect(&e);f.position(32767,32767);f.start(id);
 TEST_ASSERT_EQUAL_INT32(0,f.force(1));
}
void directed_friction_must_not_resist_perpendicular_motion() {
 Fixture f;auto id=f.effect(ET_FRICTION,1000,DIRECTION_ENABLE,6000);f.condition(id);f.position(100,173);f.start(id);
 TEST_ASSERT_INT_WITHIN(2,0,f.force());
}
void infinite_periodic_effect_must_preserve_phase_after_hours() {
 Fixture f;auto id=f.effect(ET_SINE,USB_DURATION_INFINITE,DIRECTION_ENABLE,9000);SetPeriodicReportData p{id,4000,0,0,10};f.h.set_periodic(&p);f.start(id);
 ms=5;auto initial=f.force();ms=16777215;TEST_ASSERT_INT_WITHIN(1,initial,f.force());
}
#endif
void motion_metrics_pointer_must_have_float_alignment() {
 Fixture f;
 TEST_ASSERT_EQUAL_UINT32(0,reinterpret_cast<uintptr_t>(f.input.get_position()) % alignof(float));
}
void reset_is_enabled_as_required_by_hid_pid_not_directinput_api() {
 Fixture f;f.control(2);f.control(4);TEST_ASSERT_EQUAL_UINT8(FFBReportHandler::DEVICE_STATE_ACTIVE,f.h.deviceState);
}
void pending_pid_reports_preserve_each_effect_and_retry() {
 Fixture f;auto a=f.effect(ET_CONSTANT),b=f.effect(ET_CONSTANT);f.start(a);f.start(b);
 PIDStateReportData report{};uint32_t revision=0;
 TEST_ASSERT_TRUE(f.h.peek_pid_state_report(report,revision));TEST_ASSERT_EQUAL_UINT8((a<<1)|1,report.effectBlockIndex);
 EffectOperationReportData stop{a,3,1};f.h.set_effect_operation(&stop);
 f.h.acknowledge_pid_state_report(report,revision);
 TEST_ASSERT_TRUE(f.h.peek_pid_state_report(report,revision));TEST_ASSERT_EQUAL_UINT8(a<<1,report.effectBlockIndex);
 f.h.acknowledge_pid_state_report(report,revision);
 TEST_ASSERT_TRUE(f.h.peek_pid_state_report(report,revision));TEST_ASSERT_EQUAL_UINT8((b<<1)|1,report.effectBlockIndex);
 f.h.acknowledge_pid_state_report(report,revision);TEST_ASSERT_FALSE(f.h.pidStateDirty);
}
void trigger_loops_complete_after_release() {
 Fixture f;auto id=f.effect(ET_CONSTANT,10,X_AXIS_ENABLE,0,0,1);f.constant(id);
 EffectOperationReportData op{id,1,3};f.h.set_effect_operation(&op);
 TEST_ASSERT_TRUE(f.h.is_effect_playing(id,1,0));
 TEST_ASSERT_TRUE(f.h.is_effect_playing(id,0,20));
 TEST_ASSERT_FALSE(f.h.is_effect_playing(id,0,30));
 TEST_ASSERT_EQUAL_UINT8(id<<1,f.h.get_pid_state_report_data()->effectBlockIndex);
}
void delayed_pid_state_only_plays_after_delay() {
 Fixture f;auto id=f.effect(ET_CONSTANT);auto e=f.h.get_all_effect_blocks()[id-1].effectData;e.startDelay=10;f.h.set_effect(&e);f.start(id);
 TEST_ASSERT_EQUAL_UINT8(id<<1,f.h.get_pid_state_report_data()->effectBlockIndex);
 TEST_ASSERT_FALSE(f.h.is_effect_playing(id,0,9));TEST_ASSERT_TRUE(f.h.is_effect_playing(id,0,10));
 TEST_ASSERT_EQUAL_UINT8((id<<1)|1,f.h.get_pid_state_report_data()->effectBlockIndex);
}
void sample_cache_invalidates_on_parameter_change_and_loop_restart() {
 Fixture f;auto id=f.effect(ET_SPRING,10,X_AXIS_ENABLE,0,100);f.condition(id);f.position(0);
 EffectOperationReportData op{id,1,3};f.h.set_effect_operation(&op);
 TEST_ASSERT_EQUAL_INT32(0,f.force());
 ms=5;f.position(32767);TEST_ASSERT_EQUAL_INT32(0,f.force());
 ms=10;TEST_ASSERT_EQUAL_INT32(-10000,f.force());
 f.condition(id,0,0,2000);TEST_ASSERT_EQUAL_INT32(-2000,f.force());
}
void disabled_and_paused_device_freezes_then_resumes_timer() {
 Fixture f;auto id=f.effect(ET_CONSTANT,10);f.start(id);f.control(2);ms=5;f.control(5);
 ms=100;TEST_ASSERT_EQUAL_INT32(0,f.force());TEST_ASSERT_TRUE(f.h.get_all_effect_blocks()[id-1].state & EFFECT_STATE_PLAYING);
 f.control(6);ms=104;f.force();TEST_ASSERT_TRUE(f.h.get_all_effect_blocks()[id-1].state & EFFECT_STATE_PLAYING);
 ms=105;f.force();TEST_ASSERT_FALSE(f.h.get_all_effect_blocks()[id-1].state & EFFECT_STATE_PLAYING);
}
void zero_sustain_constant_fade_has_absolute_amplitude() {
 Fixture f;auto id=f.effect(ET_CONSTANT,100);f.constant(id,0);
 auto& block=f.h.get_all_effect_blocks()[id-1];for(int i=0;i<NUM_AXIS;++i)block.directionUnitVector[i]=i==0?1.f:0.f;
 SetEnvelopeReportData env{id,0,4000,0,100};f.h.set_envelope(&env);f.start(id);
 ms=50;TEST_ASSERT_EQUAL_INT32(2000,f.force());
}
void descriptor_angles_apply_without_direction_enable() {
 Fixture f;auto id=f.effect(ET_CONSTANT,1000,X_AXIS_ENABLE,0);f.constant(id);f.start(id);
#if NUM_AXIS == 1
 TEST_ASSERT_EQUAL_INT32(4000,f.force());
#elif NUM_AXIS == 3
 TEST_ASSERT_EQUAL_INT32(-4000,f.force());
#else
 TEST_ASSERT_INT_WITHIN(1,0,f.force());
#endif
}
void disabled_actuators_still_process_trigger_and_expiry() {
 Fixture f;auto id=f.effect(ET_CONSTANT,10,X_AXIS_ENABLE,0,0,1);f.constant(id);f.start(id);f.control(2);
 f.input.update_buttons(1);TEST_ASSERT_EQUAL_INT32(0,f.force());
 TEST_ASSERT_EQUAL_UINT8((id<<1)|1,f.h.get_pid_state_report_data()->effectBlockIndex);
 f.input.update_buttons(0);ms=10;TEST_ASSERT_EQUAL_INT32(0,f.force());
 TEST_ASSERT_EQUAL_UINT8(id<<1,f.h.get_pid_state_report_data()->effectBlockIndex);
}
void stop_all_preserves_pending_stopped_handles() {
 Fixture f;auto a=f.effect(ET_CONSTANT),b=f.effect(ET_CONSTANT);f.start(a);f.start(b);f.control(3);
 PIDStateReportData report{};uint32_t revision;
 TEST_ASSERT_TRUE(f.h.peek_pid_state_report(report,revision));TEST_ASSERT_EQUAL_UINT8(a<<1,report.effectBlockIndex);f.h.acknowledge_pid_state_report(report,revision);
 TEST_ASSERT_TRUE(f.h.peek_pid_state_report(report,revision));TEST_ASSERT_EQUAL_UINT8(b<<1,report.effectBlockIndex);f.h.acknowledge_pid_state_report(report,revision);
 TEST_ASSERT_FALSE(f.h.pidStateDirty);
}
int main() {UNITY_BEGIN();
 RUN_TEST(sample_cache_invalidates_on_parameter_change_and_loop_restart);
 RUN_TEST(disabled_and_paused_device_freezes_then_resumes_timer);
 RUN_TEST(zero_sustain_constant_fade_has_absolute_amplitude);
 RUN_TEST(descriptor_angles_apply_without_direction_enable);
 RUN_TEST(disabled_actuators_still_process_trigger_and_expiry);
 RUN_TEST(stop_all_preserves_pending_stopped_handles);

 RUN_TEST(pending_pid_reports_preserve_each_effect_and_retry);
 RUN_TEST(trigger_loops_complete_after_release);
 RUN_TEST(delayed_pid_state_only_plays_after_delay);
 RUN_TEST(condition_center_must_use_descriptor_scale);
 RUN_TEST(condition_deadband_must_use_descriptor_scale);
 RUN_TEST(saturation_follows_metric_side_even_with_negative_coefficient);
#if NUM_AXIS == 2
 RUN_TEST(button_release_must_not_cancel_started_playback);
 RUN_TEST(pid_state_must_identify_second_effect_transition);
 RUN_TEST(stopped_pid_state_must_keep_the_effect_handle);
 RUN_TEST(disabled_actuators_must_still_advance_effect_lifecycle);
 RUN_TEST(condition_sample_period_must_hold_output_between_samples);
 RUN_TEST(zero_sustain_periodic_effect_must_apply_attack_amplitude);
 RUN_TEST(direction_enable_is_not_a_coordinate_format_selector);
 RUN_TEST(directed_condition_must_ignore_stale_second_axis_block);
 RUN_TEST(directed_friction_must_not_resist_perpendicular_motion);
 RUN_TEST(infinite_periodic_effect_must_preserve_phase_after_hours);
#endif
 RUN_TEST(motion_metrics_pointer_must_have_float_alignment);
 RUN_TEST(reset_is_enabled_as_required_by_hid_pid_not_directinput_api);
 UNITY_END();return 0;}
