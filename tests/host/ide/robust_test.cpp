// --- robust --- Run/Stop Program states and the modal-dialog reactions (src/ide/Robust.*)
#include "aftest.h"
#include "Robust.h"

using namespace af::robust;

TEST(run_idle_stop_does_nothing)
{
    RunModel m;
    CHECK(!m.running());
    CHECK_EQ(m.stop(0), (int)RA_NONE);
    CHECK(m.state() == RunState::Idle);
    CHECK_EQ(m.status(), std::string());
}

TEST(run_stop_sends_ctrl_c_and_arms_timer)
{
    RunModel m(3000);
    m.started("clock");
    CHECK(m.state() == RunState::Running);
    CHECK(m.status().find("Running clock") == 0);
    CHECK(m.status().find("Shift-Ctrl-F5") != std::string::npos);
    CHECK_EQ(m.stop(1000), RA_SEND_CTRL_C | RA_ARM_TIMER);
    CHECK(m.state() == RunState::Stopping);
    CHECK(!m.alsoDEF());
    CHECK(m.status().find("Sent Ctrl-C") == 0);
}

TEST(run_exit_after_stop_says_stopped)
{
    RunModel m;
    m.started("clock");
    m.stop(0);
    CHECK_EQ(m.exited(5), 0);
    CHECK(m.state() == RunState::Idle);
    CHECK(m.stopWasAsked());
    CHECK_EQ(m.endedStatus(), std::string("clock stopped (return code 5)"));
}

TEST(run_exit_on_its_own_says_ended)
{
    RunModel m;
    m.started("hello");
    CHECK_EQ(m.exited(0), 0);
    CHECK(!m.stopWasAsked());
    CHECK_EQ(m.endedStatus(), std::string("hello ended (return code 0)"));
}

TEST(run_timer_early_rearms_late_offers)
{
    RunModel m(3000);
    m.started("loop");
    m.stop(10000);
    CHECK_EQ(m.tick(11000), (int)RA_ARM_TIMER);        // woke early
    CHECK(m.state() == RunState::Stopping);
    CHECK_EQ(m.tick(13000), (int)RA_OFFER_MORE);
    CHECK(m.state() == RunState::Stubborn);
    CHECK(m.stubbornText(13000).find("3 seconds") != std::string::npos);
    CHECK(m.stubbornText(13000).find("can't force") != std::string::npos);
}

TEST(run_second_stop_within_grace_waits)
{
    RunModel m(3000);
    m.started("loop");
    m.stop(0);
    CHECK_EQ(m.stop(1000), (int)RA_NONE);
    CHECK_EQ(m.stop(4000), (int)RA_OFFER_MORE);         // no need to wait for the timer
    CHECK_EQ(m.stop(4500), (int)RA_OFFER_MORE);         // and again while stubborn
}

TEST(run_choices)
{
    RunModel m(3000);
    m.started("loop");
    m.stop(0);
    m.tick(3000);
    CHECK_EQ(m.choose(SC_ALL, 3000), RA_SEND_CTRL_C | RA_ARM_TIMER);
    CHECK(m.alsoDEF());
    CHECK(m.state() == RunState::Stopping);
    CHECK_EQ(m.tick(6000), (int)RA_OFFER_MORE);
    CHECK_EQ(m.choose(SC_AGAIN, 6000), RA_SEND_CTRL_C | RA_ARM_TIMER);
    CHECK(!m.alsoDEF());
    m.tick(9000);
    CHECK_EQ(m.choose(SC_LEAVE, 9000), (int)RA_NONE);
    CHECK(m.state() == RunState::Running);
    CHECK_EQ(m.tick(20000), (int)RA_NONE);              // a stale timer does nothing
    CHECK_EQ(m.stop(20000), RA_SEND_CTRL_C | RA_ARM_TIMER);   // starts over
}

TEST(run_choice_after_exit_is_ignored)
{
    RunModel m;
    m.started("loop");
    m.stop(0);
    m.tick(5000);
    m.exited(0);
    CHECK_EQ(m.choose(SC_ALL, 5000), (int)RA_NONE);
    CHECK(m.state() == RunState::Idle);
}

TEST(run_several_programs)
{
    RunModel m;
    m.started("a");
    m.started("b");
    CHECK_EQ(m.programs(), 2);
    CHECK(m.status().find("Running b (and 1 other)") == 0);
    m.started("c");
    CHECK(m.status().find("(and 2 others)") != std::string::npos);
    m.stop(0);
    CHECK_EQ(m.exited(0), 2);
    CHECK(m.state() == RunState::Stopping);             // still waiting for the others
    CHECK(m.stubbornText(5000).find("other programs") != std::string::npos);
    CHECK_EQ(m.exited(0), 1);
    CHECK_EQ(m.exited(0), 0);
    CHECK(!m.running());
}

TEST(run_timer_wraps)
{
    RunModel m(3000);
    m.started("x");
    m.stop(0xFFFFF000u);                                // millisecond counter about to wrap
    CHECK_EQ(m.tick(0xFFFFF800u), (int)RA_ARM_TIMER);   // 2048 ms later
    CHECK_EQ(m.tick(0x00000100u), (int)RA_OFFER_MORE);  // 4352 ms later, past the wrap
}

TEST(modal_reactions)
{
    CHECK(modalReaction(4u << 16) == ModalReaction::RaiseDialog);    // WMHI_ACTIVE
    CHECK(modalReaction(2u << 16) == ModalReaction::RaiseDialog);    // WMHI_GADGETUP
    CHECK(modalReaction(6u << 16) == ModalReaction::RaiseDialog);    // WMHI_MENUPICK
    CHECK(modalReaction(1u << 16) == ModalReaction::RaiseDialog);    // WMHI_CLOSEWINDOW
    CHECK(modalReaction(11u << 16) == ModalReaction::RaiseDialog);   // WMHI_RAWKEY
    CHECK(modalReaction(5u << 16) == ModalReaction::Handle);         // WMHI_NEWSIZE
    CHECK(modalReaction(3u << 16) == ModalReaction::Ignore);         // WMHI_INACTIVE
    CHECK(modalReaction(14u << 16) == ModalReaction::Ignore);        // WMHI_INTUITICK
    CHECK(modalReaction(13u << 16) == ModalReaction::Ignore);        // WMHI_CHANGEWINDOW
}
