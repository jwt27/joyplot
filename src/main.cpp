/* * * * * * * * * * * * * *  joyplot  * * * * * * * * * * * * * */
/* Copyright (C) 2018 J.W. Jagersma, see COPYING.txt for details */

#include <jw/io/gameport.h>
#include <jw/io/rs232.h>
#include <jw/io/ps2_interface.h>
#include <jw/io/keyboard.h>
#include <chrono>
#include <deque>
#include <string_view>

using namespace std::chrono_literals;
using namespace jw;
using namespace jw::io;

int jwdpmi_main(std::deque<std::string_view>)
{
    bool running = true;
    float speed = 20e3;

    chrono::setup::setup_pit(true, 0x1000);
    chrono::setup::setup_tsc(10000);
    using clock = jw::chrono::tsc;

    io::rs232_config rs232_cfg { };
    rs232_cfg.set_com_port(io::com1);
    rs232_cfg.flow_control = io::rs232_config::xon_xoff;
    auto s = io::make_rs232_stream(rs232_cfg);

    io::keyboard keyb { std::make_shared<io::ps2_interface>() };

    callback key_event { [&](key_state_pair k)
    {
        if (k.second != key_state::up)
        {
            switch (k.first)
            {
            case key::esc: running = false; break;
            case key::num_add: speed *= 1.5f; break;
            case key::num_sub: speed *= 0.75f; break;
            default:
                auto c = k.first.to_ascii(false, false, true);
                if (c > '0' and c < '9') s << "SP" << c << ';' << std::flush;
            }
        }
    } };
    keyb.key_changed += key_event;

    std::cout << "synchronizing timer..." << std::endl;
    thread::yield_for(2s);

    io::gameport::config gameport_cfg { };
    gameport_cfg.enable.z = false;
    gameport_cfg.enable.w = false;

    std::cout << "calibrate joystick, press fire when done." << std::endl;
    {
        io::gameport joystick { gameport_cfg };
        std::swap(gameport_cfg.calibration.max, gameport_cfg.calibration.min);
        do
        {
            auto raw = joystick.get_raw();
            for (auto i = 0; i < 4; ++i)
            {
                gameport_cfg.calibration.min[i] = std::min(gameport_cfg.calibration.min[i], raw[i]);
                gameport_cfg.calibration.max[i] = std::max(gameport_cfg.calibration.max[i], raw[i]);
            }
        } while (joystick.buttons().none() and running);
    }

    gameport_cfg.strategy = io::gameport::poll_strategy::thread;
    io::gameport joystick { gameport_cfg };

    std::cout << "initializing plotter... " << std::flush;
    s << "IN;PA5180,3800;" << std::flush;

    callback button_event { [&] (std::bitset<4> state, io::gameport::clock::time_point)
    {
        static std::bitset<4> last_state { 0 };
        auto x = last_state ^ state;
        if (x[0]) s << (state[0] ? "PD;" : "PU;") << std::flush;
        last_state = state;
    } };
    joystick.button_changed += button_event;

    std::cout << "ready." << std::endl;
    auto last_now = clock::now();
    while (running)
    {
        auto now = clock::now();
        float dt = (now - last_now).count() / 1e9f;
        last_now = now;

        auto j = joystick.get() * dt * speed;
        s << "PR" << static_cast<std::int16_t>(round(j.x)) << ',' << static_cast<std::int16_t>(round(j.y)) << ';' << std::flush;
        thread::yield_until(now + 1ms);
    }

    s << "IN;" << std::flush;

    return 0;
}
