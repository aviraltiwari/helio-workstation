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
#include "Instrument.h"
#include "PluginWindow.h"
#include "InternalPluginFormat.h"
#include "SerializablePluginDescription.h"
#include "SerializationKeys.h"

const int Instrument::midiChannelNumber = 0x1000;

Instrument::Instrument(AudioPluginFormatManager &formatManager, String name) :
    formatManager(formatManager),
    instrumentName(std::move(name)),
    instrumentID()
{
    this->processorGraph = new AudioProcessorGraph();
    this->initializeDefaultNodes();
    this->processorPlayer.setProcessor(this->processorGraph);
}

Instrument::~Instrument()
{
    this->processorPlayer.setProcessor(nullptr);
    
    PluginWindow::closeAllCurrentlyOpenWindows();
    this->processorGraph->clear();
    this->processorGraph = nullptr;
}


String Instrument::getName() const
{
    return this->instrumentName;
}

void Instrument::setName(const String &name)
{
    this->instrumentName = name;
}

String Instrument::getInstrumentID() const
{
    return this->instrumentID.toString();
}

String Instrument::getInstrumentHash() const
{
    // для одного и того же инструмента на разных платформах этот хэш будет одинаковым
    // но если создать два инструмента с одним и тем же плагином - хэш тоже будет одинаковым
    // поэтому в слое мы храним id и хэш
    
    //const double t1 = Time::getMillisecondCounterHiRes();
    
    String iID;
    const int numNodes = this->processorGraph->getNumNodes();
    
    for (int i = 0; i < numNodes; ++i)
    {
        const String &nodeHash = this->processorGraph->getNode(i)->properties["hash"].toString();
        iID += nodeHash;
    }
    
    const String &hash = MD5(iID.toUTF8()).toHexString();
    
    //const double t2 = Time::getMillisecondCounterHiRes();
    //Logger::writeToLog(String(t2 - t1));
    
    return hash;
}

String Instrument::getIdAndHash() const
{
    return this->getInstrumentID() + this->getInstrumentHash();
}


void Instrument::initializeFrom(const PluginDescription &pluginDescription, InitializationCallback initCallback)
{
    this->processorGraph->clear();
    this->initializeDefaultNodes();
    
    this->addNodeAsync(pluginDescription, 0.5f, 0.5f, 
        [initCallback, this](AudioProcessorGraph::Node::Ptr instrument)
        {
            if (instrument == nullptr) { return; }

            for (int i = 0; i < instrument->getProcessor()->getTotalNumInputChannels(); ++i)
            {
                this->addConnection(this->audioIn->nodeID, i, instrument->nodeID, i);
            }

            if (instrument->getProcessor()->acceptsMidi())
            {
                this->addConnection(this->midiIn->nodeID, Instrument::midiChannelNumber, instrument->nodeID, Instrument::midiChannelNumber);
            }

            for (int i = 0; i < instrument->getProcessor()->getTotalNumOutputChannels(); ++i)
            {
                this->addConnection(instrument->nodeID, i, this->audioOut->nodeID, i);
            }

            if (instrument->getProcessor()->producesMidi())
            {
                this->addConnection(instrument->nodeID, Instrument::midiChannelNumber, this->midiOut->nodeID, Instrument::midiChannelNumber);
            }

            initCallback(this);
            this->sendChangeMessage();
        });
}

void Instrument::addNodeToFreeSpace(const PluginDescription &pluginDescription, InitializationCallback initCallback)
{
    // TODO: find free space on a canvas
    float x = 0.5f;
    float y = 0.5f;

    this->addNodeAsync(pluginDescription, x, y, [initCallback, this](AudioProcessorGraph::Node::Ptr node)
    {
        if (node != nullptr)
        {
            initCallback(this);
            this->sendChangeMessage();
        }
    });
}


//===----------------------------------------------------------------------===//
// Nodes
//===----------------------------------------------------------------------===//

int Instrument::getNumNodes() const noexcept
{
    return this->processorGraph->getNumNodes();
}

const AudioProcessorGraph::Node::Ptr Instrument::getNode(int index) const noexcept
{
    return this->processorGraph->getNode(index);
}

const AudioProcessorGraph::Node::Ptr Instrument::getNodeForId(AudioProcessorGraph::NodeID uid) const noexcept
{
    return this->processorGraph->getNodeForId(uid);
}

void Instrument::addNodeAsync(const PluginDescription &desc,
    double x, double y, AddNodeCallback f)
{
    this->formatManager.createPluginInstanceAsync(desc,
        this->processorGraph->getSampleRate(),
        this->processorGraph->getBlockSize(),
        [this, desc, x, y, f](AudioPluginInstance *instance, const String &error)
    {
        AudioProcessorGraph::Node::Ptr node = nullptr;

        if (instance != nullptr)
        {
            node = this->processorGraph->addNode(instance);
        }

        if (node == nullptr)
        {
            f(nullptr);
            return;
        }

        this->configureNode(node, desc, x, y);
        this->sendChangeMessage();

        f(node);
    });
}

AudioProcessorGraph::Node::Ptr Instrument::addNode(Instrument *instrument, double x, double y)
{
    AudioProcessorGraph::Node::Ptr node =
        this->processorGraph->addNode(instrument->getProcessorGraph());
    
    if (node != nullptr)
    {
        node->properties.set(Serialization::UI::positionX, x);
        node->properties.set(Serialization::UI::positionY, y);
        this->sendChangeMessage();
    }

    return node;
}

void Instrument::removeNode(AudioProcessorGraph::NodeID id)
{
    PluginWindow::closeCurrentlyOpenWindowsFor(id);
    this->processorGraph->removeNode(id);
    this->sendChangeMessage();
}

void Instrument::disconnectNode(AudioProcessorGraph::NodeID id)
{
    this->processorGraph->disconnectNode(id);
    this->sendChangeMessage();
}

void Instrument::removeIllegalConnections()
{
    this->processorGraph->removeIllegalConnections();
    this->sendChangeMessage();
}

void Instrument::setNodePosition(AudioProcessorGraph::NodeID id, double x, double y)
{
    const AudioProcessorGraph::Node::Ptr n(this->processorGraph->getNodeForId(id));

    if (n != nullptr)
    {
        n->properties.set(Serialization::UI::positionX, jlimit(0.0, 1.0, x));
        n->properties.set(Serialization::UI::positionY, jlimit(0.0, 1.0, y));
    }
}

void Instrument::getNodePosition(AudioProcessorGraph::NodeID id, double &x, double &y) const
{
    x = y = 0;
    const AudioProcessorGraph::Node::Ptr n(this->processorGraph->getNodeForId(id));
    if (n != nullptr)
    {
        x = (double) n->properties[Serialization::UI::positionX];
        y = (double) n->properties[Serialization::UI::positionY];
    }
}


//===----------------------------------------------------------------------===//
// Default nodes' id's
//===----------------------------------------------------------------------===//

AudioProcessorGraph::NodeID Instrument::getMidiInId() const
{
    jassert(this->midiIn);
    return this->midiIn->nodeID;
}

AudioProcessorGraph::NodeID Instrument::getMidiOutId() const
{
    jassert(this->midiOut);
    return this->midiOut->nodeID;
}

AudioProcessorGraph::NodeID Instrument::getAudioInId() const
{
    jassert(this->audioIn);
    return this->audioIn->nodeID;
}

AudioProcessorGraph::NodeID Instrument::getAudioOutId() const
{
    jassert(this->audioOut);
    return this->audioOut->nodeID;
}

bool Instrument::isNodeStandardInputOrOutput(AudioProcessorGraph::NodeID nodeId) const
{
    return nodeId == this->getMidiInId()
        || nodeId == this->getMidiOutId()
        || nodeId == this->getAudioInId()
        || nodeId == this->getAudioOutId();
}


//===----------------------------------------------------------------------===//
// Connections
//===----------------------------------------------------------------------===//

std::vector<AudioProcessorGraph::Connection> Instrument::getConnections() const noexcept
{
    return this->processorGraph->getConnections();
}

bool Instrument::isConnected(AudioProcessorGraph::Connection connection) const noexcept
{
    return this->processorGraph->isConnected(connection);
}

bool Instrument::canConnect(AudioProcessorGraph::Connection connection) const noexcept
{
    return this->processorGraph->canConnect(connection);
}

bool Instrument::addConnection(AudioProcessorGraph::NodeID sourceID, int sourceChannel,
    AudioProcessorGraph::NodeID destinationID, int destinationChannel)
{
    AudioProcessorGraph::NodeAndChannel source;
    source.nodeID = sourceID;
    source.channelIndex = sourceChannel;

    AudioProcessorGraph::NodeAndChannel destination;
    destination.nodeID = destinationID;
    destination.channelIndex = destinationChannel;

    AudioProcessorGraph::Connection c(source, destination);
    if (this->processorGraph->addConnection(c))
    {
        this->sendChangeMessage();
        return true;
    }

    return false;
}

void Instrument::removeConnection(AudioProcessorGraph::Connection connection)
{
    this->processorGraph->removeConnection(connection);
    this->sendChangeMessage();
}

void Instrument::reset()
{
    PluginWindow::closeAllCurrentlyOpenWindows();
    this->processorGraph->clear();
    this->sendChangeMessage();
}


//===----------------------------------------------------------------------===//
// Serializable
//===----------------------------------------------------------------------===//

ValueTree Instrument::serialize() const
{
    using namespace Serialization;
    ValueTree tree(Audio::instrument);
    tree.setProperty(Audio::instrumentId, this->instrumentID.toString(), nullptr);
    tree.setProperty(Audio::instrumentName, this->instrumentName, nullptr);

    const int numNodes = this->processorGraph->getNumNodes();
    for (int i = 0; i < numNodes; ++i)
    {
        tree.appendChild(this->serializeNode(this->processorGraph->getNode(i)), nullptr);
    }

    for (const auto &c : this->getConnections())
    {
        ValueTree e(Audio::connection);
        e.setProperty(Audio::sourceNodeId, static_cast<int>(c.source.nodeID), nullptr);
        e.setProperty(Audio::sourceChannel, c.source.channelIndex, nullptr);
        e.setProperty(Audio::destinationNodeId, static_cast<int>(c.destination.nodeID), nullptr);
        e.setProperty(Audio::destinationChannel, c.destination.channelIndex, nullptr);
        tree.appendChild(e, nullptr);
    }

    return tree;
}

void Instrument::deserialize(const ValueTree &tree)
{
    this->reset();
    using namespace Serialization;

    const auto root = tree.hasType(Audio::instrument) ?
        tree : tree.getChildWithName(Audio::instrument);

    if (!root.isValid())
    { return; }

    this->instrumentID = root.getProperty(Audio::instrumentId, this->instrumentID.toString());
    this->instrumentName = root.getProperty(Audio::instrumentName, this->instrumentName);

    // Well this hack of an incredible ugliness
    // is here to handle loading of async-loaded AUv3 plugins
    
    // Fill up the connections info for further processing
    struct ConnectionDescription final
    {
        const uint32 sourceNodeId;
        const uint32 destinationNodeId;
        const int sourceChannel;
        const int destinationChannel;
    };
    
    Array<ConnectionDescription> connectionDescriptions;
    
    forEachValueTreeChildWithType(root, e, Audio::connection)
    {
        const uint32 sourceNodeId = static_cast<int>(e.getProperty(Audio::sourceNodeId));
        const uint32 destinationNodeId = static_cast<int>(e.getProperty(Audio::destinationNodeId));
        connectionDescriptions.add({
            sourceNodeId,
            destinationNodeId,
            e.getProperty(Audio::sourceChannel),
            e.getProperty(Audio::destinationChannel)
        });
    }
    
    forEachValueTreeChildWithType(root, e, Serialization::Audio::node)
    {
        this->deserializeNodeAsync(e,
            [this, connectionDescriptions](AudioProcessorGraph::Node::Ptr)
            {
                // Try to create as many connections as possible
                for (const auto &connectionInfo : connectionDescriptions)
                {
                    this->addConnection(connectionInfo.sourceNodeId, connectionInfo.sourceChannel,
                                        connectionInfo.destinationNodeId, connectionInfo.destinationChannel);
                }
                                         
                this->processorGraph->removeIllegalConnections();
                this->sendChangeMessage();
            });
    }
}

ValueTree Instrument::serializeNode(AudioProcessorGraph::Node::Ptr node) const
{
    using namespace Serialization;
    if (AudioPluginInstance *plugin = dynamic_cast<AudioPluginInstance *>(node->getProcessor()))
    {
        ValueTree tree(Audio::node);
        tree.setProperty(Audio::nodeId, static_cast<int>(node->nodeID), nullptr);
        tree.setProperty(Audio::nodeHash, node->properties[Audio::nodeHash].toString(), nullptr);
        tree.setProperty(UI::positionX, node->properties[UI::positionX].toString(), nullptr);
        tree.setProperty(UI::positionY, node->properties[UI::positionY].toString(), nullptr);

        SerializablePluginDescription pd;
        plugin->fillInPluginDescription(pd);

        tree.appendChild(pd.serialize(), nullptr);

        MemoryBlock m;
        node->getProcessor()->getStateInformation(m);
        tree.setProperty(Serialization::Audio::pluginState, m.toBase64Encoding(), nullptr);

        return tree;
    }
    
    return {};
}

void Instrument::deserializeNodeAsync(const ValueTree &tree, AddNodeCallback f)
{
    using namespace Serialization;
    SerializablePluginDescription pd;
    for (const auto &e : tree)
    {
        pd.deserialize(e);
        if (pd.isValid()) { break; }
    }
    
    MemoryBlock nodeStateBlock;
    const String state = tree.getProperty(Audio::pluginState);
    if (state.isNotEmpty())
    {
        nodeStateBlock.fromBase64Encoding(state);
    }
    
    const int nodeUid = tree.getProperty(Audio::nodeId);
    const String nodeHash = tree.getProperty(Audio::nodeHash);
    const double nodeX = tree.getProperty(UI::positionX);
    const double nodeY = tree.getProperty(UI::positionY);
    
    formatManager.
    createPluginInstanceAsync(pd,
        this->processorGraph->getSampleRate(),
        this->processorGraph->getBlockSize(),
        [this, nodeStateBlock, nodeUid, nodeHash, nodeX, nodeY, f]
        (AudioPluginInstance *instance, const String &error)
        {
            if (instance == nullptr)
            {
                f(nullptr);
                return;
            }

            AudioProcessorGraph::Node::Ptr node(this->processorGraph->addNode(instance, nodeUid));

            if (nodeStateBlock.getSize() > 0)
            {
                node->getProcessor()->
                    setStateInformation(nodeStateBlock.getData(),
                        static_cast<int>(nodeStateBlock.getSize()));
            }

            Uuid fallbackRandomHash;
            const auto hash = nodeHash.isNotEmpty() ? nodeHash : fallbackRandomHash.toString();
            node->properties.set(Audio::nodeHash, hash);
            node->properties.set(UI::positionX, nodeX);
            node->properties.set(UI::positionY, nodeY);
            f(node);
        });
}

void Instrument::deserializeNode(const ValueTree &tree)
{
    using namespace Serialization;
    SerializablePluginDescription pd;
    for (const auto &e : tree)
    {
        pd.deserialize(e);
        if (pd.isValid()) { break; }
    }
    
    String errorMessage;

    AudioPluginInstance *instance =
        formatManager.createPluginInstance(pd,
            this->processorGraph->getSampleRate(),
            this->processorGraph->getBlockSize(),
            errorMessage);

    if (instance == nullptr)
    {
        return;
    }

    const int nodeUid = tree.getProperty(Audio::nodeId);
    AudioProcessorGraph::Node::Ptr node(this->processorGraph->addNode(instance, nodeUid));

    const String state = tree.getProperty(Audio::pluginState);
    if (state.isNotEmpty())
    {
        MemoryBlock m;
        m.fromBase64Encoding(state);
        node->getProcessor()->setStateInformation(m.getData(), static_cast<int>( m.getSize()));
    }

    const String &hash = tree.getProperty(Audio::nodeHash);
    Uuid fallbackRandomHash;
    
    node->properties.set(UI::positionX, tree.getProperty(UI::positionX));
    node->properties.set(UI::positionY, tree.getProperty(UI::positionY));
    node->properties.set(Audio::nodeHash, hash.isNotEmpty() ? hash : fallbackRandomHash.toString());
}

void Instrument::initializeDefaultNodes()
{
    InternalPluginFormat internalFormat;
    this->audioIn = this->addDefaultNode(*internalFormat.getDescriptionFor(InternalPluginFormat::audioInputFilter), 0.1f, 0.15f);
    this->midiIn = this->addDefaultNode(*internalFormat.getDescriptionFor(InternalPluginFormat::midiInputFilter), 0.1f, 0.85f);
    this->audioOut = this->addDefaultNode(*internalFormat.getDescriptionFor(InternalPluginFormat::audioOutputFilter), 0.9f, 0.15f);
    this->midiOut = this->addDefaultNode(*internalFormat.getDescriptionFor(InternalPluginFormat::midiOutputFilter), 0.9f, 0.85f);
}

AudioProcessorGraph::Node::Ptr Instrument::addDefaultNode(
    const PluginDescription &desc, double x, double y)
{
    String errorMessage;
    AudioPluginInstance *instance =
    formatManager.createPluginInstance(desc,
        this->processorGraph->getSampleRate(),
        this->processorGraph->getBlockSize(),
        errorMessage);
    
    AudioProcessorGraph::Node::Ptr node = nullptr;
    
    if (instance != nullptr)
    {
        node = this->processorGraph->addNode(instance);
    }
    
    if (node != nullptr)
    {
        this->configureNode(node, desc, x, y);
        this->sendChangeMessage();
        return node;
    }
    
    
    return nullptr;
}

void Instrument::configureNode(AudioProcessorGraph::Node::Ptr node,
    const PluginDescription &desc, double x, double y)
{
    // make a hash from a general instrument description
    const String descriptionString = (desc.name +
                                      desc.category +
                                      desc.descriptiveName +
                                      desc.manufacturerName +
                                      desc.pluginFormatName +
                                      String(desc.numInputChannels) +
                                      String(desc.numOutputChannels));
    
    const String nodeHash = MD5(descriptionString.toUTF8()).toHexString();
    
    node->properties.set(Serialization::Audio::nodeHash, nodeHash);
    node->properties.set(Serialization::UI::positionX, x);
    node->properties.set(Serialization::UI::positionY, y);
}
