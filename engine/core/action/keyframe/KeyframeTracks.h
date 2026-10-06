// (c) Eduardo Doria and contributors
// SPDX-License-Identifier: MIT

#ifndef KEYFRAMETRACKS_H
#define KEYFRAMETRACKS_H

#include "action/Action.h"
#include "action/Ease.h"

namespace doriax{
    class DORIAX_API KeyframeTracks: public Action{

    public:
        KeyframeTracks(Scene* scene);
        KeyframeTracks(Scene* scene, Entity entity);

        void setTimes(std::vector<float> times);
        void setEasings(std::vector<EaseType> easings);
        void setEasing(unsigned int segment, EaseType ease);

        void setLoop(bool loop);
        bool isLoop() const;

        void setRelative(bool relative);
        bool isRelative() const;
    };
}

#endif //KEYFRAMETRACKS_H
