// -*- mode: c++; c-indent-level: 4; c++-member-init-indent: 8; comment-column: 35; -*-

//-----------------------------------------------------------------------------
// eoSecondsElapsedTrackGenContinue.h
// (c) Eremey Valetov, 2024
/*
    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

    Contact: todos@geneura.ugr.es, http://geneura.ugr.es
*/
//-----------------------------------------------------------------------------

#ifndef _eoSecondsElapsedTrackGenContinue_h
#define _eoSecondsElapsedTrackGenContinue_h

#include "eoContinue.h"
#include "utils/eoParam.h"
#include "utils/eoLogger.h"
#include <ctime>
#include <iomanip>
#include <sstream>

/**
    Timed continuator with generation tracking: continues until a number of
    seconds have elapsed, counting generations and logging elapsed time
    in HH:MM:SS format.

    @ingroup Continuators
*/
template<class EOT>
class eoSecondsElapsedTrackGenContinue : public eoContinue<EOT>, public eoValueParam<unsigned> {
public:

    eoSecondsElapsedTrackGenContinue(time_t _seconds)
        : eoValueParam<unsigned>(0, "eoSecondsElapsedTrackGenContinue"),
          start(time(0)), stop(0), seconds(_seconds), hasStopped(false) {}

    bool operator()(const eoPop<EOT>&) override {
        value()++;

        time_t now = time(0);
        time_t diff = now - start;

        if (diff >= seconds) {
            stop = now;
            hasStopped = true;
            eo::log << eo::logging << "Time elapsed: " << formatTime(diff)
                    << ", Generations: " << value() << "\n";
            if (value() == 1) {
                eo::log << eo::warnings
                        << "Time limit exceeded at first generation check ("
                        << formatTime(diff) << " elapsed)." << std::endl;
            }
            return false;
        }
        return true;
    }

    std::string className() const override {
        return "eoSecondsElapsedTrackGenContinue";
    }

    void readFrom(std::istream& _is) override {
        _is >> start >> seconds;
    }

    void printOn(std::ostream& _os) const override {
        time_t elapsed = hasStopped ? stop - start : time(0) - start;
        _os << formatTime(elapsed) << " elapsed (" << elapsed << " s), "
            << value() << " generations";
    }

private:
    time_t start;
    time_t stop;
    time_t seconds;
    bool hasStopped;

    std::string formatTime(time_t totalSeconds) const {
        std::ostringstream oss;
        int h = totalSeconds / 3600;
        int m = (totalSeconds % 3600) / 60;
        int s = totalSeconds % 60;
        oss << std::setfill('0') << std::setw(2) << h << ":"
            << std::setfill('0') << std::setw(2) << m << ":"
            << std::setfill('0') << std::setw(2) << s;
        return oss.str();
    }
};

#endif
