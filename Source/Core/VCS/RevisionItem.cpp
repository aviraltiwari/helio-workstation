/*
    This file is part of Helio Workstation.

    Helio is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    Helio is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with Helio. If not, see <http://www.gnu.org/licenses/>.
*/

#include "Common.h"
#include "RevisionItem.h"
#include "DiffLogic.h"

using namespace VCS;

RevisionItem::RevisionItem(Pack::Ptr packPtr, Type type, TrackedItem *targetToCopy) :
    vcsItemType(type),
    pack(packPtr)
{
    jassert(packPtr.get() != nullptr);

    if (targetToCopy != nullptr)
    {
        this->description = targetToCopy->getVCSName();
        this->vcsUuid = targetToCopy->getUuid();

        this->logic = DiffLogic::createLogicCopy(*targetToCopy, *this);

        // just deep-copy all deltas:
        for (int i = 0; i < targetToCopy->getNumDeltas(); ++i)
        {
            const auto targetDelta = targetToCopy->getDelta(i);
            this->deltas.add(targetDelta->createCopy());
            this->deltasData.add(targetToCopy->serializeDeltaData(i));
        }
    }
}

void RevisionItem::flushData()
{
    for (int i = 0; i < this->deltasData.size(); ++i)
    {
        this->pack->setDeltaDataFor(this->getUuid(), this->deltas[i]->getUuid(), this->deltasData[i]);
    }

    this->deltasData.clear();
}

Pack::Ptr RevisionItem::getPackPtr() const
{
    return this->pack;
}

RevisionItem::Type RevisionItem::getType() const
{
    return this->vcsItemType;
}

String RevisionItem::getTypeAsString() const
{
    if (this->vcsItemType == Added)
    {
        return TRANS("vcs::delta::type::added");
    }
    if (this->vcsItemType == Removed)
    {
        return TRANS("vcs::delta::type::removed");
    }
    else if (this->vcsItemType == Changed)
    {
        return TRANS("vcs::delta::type::changed");
    }

    return {};
}

void VCS::RevisionItem::importDataForDelta(const ValueTree &deltaDataToCopy, const String &deltaUuid)
{
    for (int i = 0; i < this->deltas.size(); ++i)
    {
        const Delta *delta = this->deltas[i];
        
        if (delta->getUuid().toString() == deltaUuid)
        {
            while (this->deltasData.size() <= i)
            {
                this->deltasData.add(ValueTree("dummy"));
            }
            
            const ValueTree deepCopy(deltaDataToCopy.createCopy());
            this->deltasData.set(i, deepCopy);
            break;
        }
    }
}


//===----------------------------------------------------------------------===//
// TrackedItem
//===----------------------------------------------------------------------===//

int RevisionItem::getNumDeltas() const
{
    return this->deltas.size();
}

Delta *RevisionItem::getDelta(int index) const
{
    return this->deltas[index];
}

ValueTree VCS::RevisionItem::serializeDeltaData(int deltaIndex) const
{
    if (deltaIndex < this->deltasData.size())
    {
        // at this point revision item represents uncommitted changes
        // and it already has all the data:
        return ValueTree(this->deltasData[deltaIndex]);
    }

    return this->pack->createDeltaDataFor(this->getUuid(), this->deltas[deltaIndex]->getUuid());
}

String RevisionItem::getVCSName() const
{
    return this->description;
}

DiffLogic *VCS::RevisionItem::getDiffLogic() const
{
    return this->logic;
}


//===----------------------------------------------------------------------===//
// Serializable
//===----------------------------------------------------------------------===//

ValueTree VCS::RevisionItem::serialize() const
{
    ValueTree tree(Serialization::VCS::revisionItem);

    this->serializeVCSUuid(tree);

    tree.setProperty(Serialization::VCS::revisionItemType, this->getType(), nullptr);
    tree.setProperty(Serialization::VCS::revisionItemName, this->getVCSName(), nullptr);
    tree.setProperty(Serialization::VCS::revisionItemDiffLogic, this->getDiffLogic()->getType().toString(), nullptr);

    for (auto delta : this->deltas)
    {
        tree.appendChild(delta->serialize(), nullptr);
    }

    return tree;
}

void VCS::RevisionItem::deserialize(const ValueTree &tree)
{
    this->reset();

    const auto root = tree.hasType(Serialization::VCS::revisionItem) ?
        tree : tree.getChildWithName(Serialization::VCS::revisionItem);

    if (!root.isValid()) { return; }

    this->deserializeVCSUuid(root);

    this->description = root.getProperty(Serialization::VCS::revisionItemName, "");

    const int type = root.getProperty(Serialization::VCS::revisionItemType, Undefined);
    this->vcsItemType = static_cast<Type>(type);

    const String logicType = root.getProperty(Serialization::VCS::revisionItemDiffLogic);

    jassert(logicType != "");

    this->logic = DiffLogic::createLogicFor(*this, logicType);

    for (const auto &e : root)
    {
        ScopedPointer<Delta> delta(new Delta({}, {}));
        delta->deserialize(e);
        this->deltas.add(delta.release());
    }
}

void RevisionItem::reset()
{
    this->deltas.clear();
    this->description = "";
    this->vcsItemType = Undefined;
}
