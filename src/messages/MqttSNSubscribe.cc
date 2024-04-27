//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Lesser General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program.  If not, see http://www.gnu.org/licenses/.
//

#include "MqttSNSubscribe.h"

namespace mqttsn {

void MqttSNSubscribe::setDupFlag(bool dupFlag)
{
    MqttSNBase::setBooleanFlag(dupFlag, Flag::DUP, flags);
}

bool MqttSNSubscribe::getDupFlag() const
{
    return MqttSNBase::getBooleanFlag(Flag::DUP, flags);
}

void MqttSNSubscribe::setQoSFlag(QoS qosFlag)
{
    MqttSNBase::setFlag(qosFlag, Flag::QUALITY_OF_SERVICE, flags);
}

uint8_t MqttSNSubscribe::getQoSFlag() const
{
    return MqttSNBase::getFlag(Flag::QUALITY_OF_SERVICE, flags);
}

} /* namespace mqttsn */
