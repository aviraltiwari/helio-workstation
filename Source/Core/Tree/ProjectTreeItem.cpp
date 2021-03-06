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
#include "ProjectTreeItem.h"

#include "TreeItemChildrenSerializer.h"
#include "RootTreeItem.h"
#include "TrackGroupTreeItem.h"
#include "PianoTrackTreeItem.h"
#include "AutomationTrackTreeItem.h"
#include "PatternEditorTreeItem.h"
#include "MainLayout.h"
#include "Document.h"
#include "DocumentHelpers.h"
#include "XmlSerializer.h"
#include "BinarySerializer.h"
#include "ProjectListener.h"
#include "ProjectPageDefault.h"
#include "ProjectPagePhone.h"
#include "AudioCore.h"
#include "PlayerThread.h"
#include "SequencerLayout.h"
#include "MidiEvent.h"
#include "MidiTrack.h"
#include "PianoTrackTreeItem.h"
#include "AutomationSequence.h"
#include "Icons.h"
#include "ProjectInfo.h"
#include "ProjectTimeline.h"
#include "TrackedItem.h"
#include "VersionControlTreeItem.h"
#include "VersionControl.h"
#include "RecentFilesList.h"
#include "HybridRoll.h"
#include "Autosaver.h"

#include "HelioTheme.h"
#include "ProjectCommandPanel.h"
#include "UndoStack.h"

#include "DocumentHelpers.h"
#include "Workspace.h"
#include "App.h"

#include "Config.h"
#include "SerializationKeys.h"


ProjectTreeItem::ProjectTreeItem(const String &name) :
    DocumentOwner(name, "helio"),
    TreeItem(name, Serialization::Core::project)
{
    this->initialize();
}

ProjectTreeItem::ProjectTreeItem(const File &existingFile) :
    DocumentOwner(existingFile),
    TreeItem(existingFile.getFileNameWithoutExtension(), Serialization::Core::project)
{
    this->initialize();
}

void ProjectTreeItem::initialize()
{
    this->isLayersHashOutdated = true;
    
    this->undoStack = new UndoStack(*this);
    
    this->autosaver = new Autosaver(*this);

    this->transport = new Transport(App::Workspace().getAudioCore());
    this->addListener(this->transport);
    
    this->recentFilesList = &App::Workspace().getRecentFilesList();
    
    this->info = new ProjectInfo(*this);
    this->vcsItems.add(this->info);
    
    this->timeline = new ProjectTimeline(*this, "Project Timeline");
    this->vcsItems.add(this->timeline);

    this->transport->seekToPosition(0.0);
    
    this->recreatePage();
}

ProjectTreeItem::~ProjectTreeItem()
{
    // the main policy: all data is to be autosaved
    this->getDocument()->save();
    
    this->transport->stopPlayback();
    this->transport->stopRender();

    // remember as the recent file
    if (this->recentFilesList != nullptr)
    {
        this->recentFilesList->
        onProjectStateChanged(this->getName(),
                              this->getDocument()->getFullPath(),
                              this->getId(),
                              false);
    }

    this->projectSettings = nullptr;

    this->removeAllListeners();
    this->sequencerLayout = nullptr;

    this->timeline = nullptr;
    this->info = nullptr;

    this->removeListener(this->transport);
    this->transport = nullptr;

    this->autosaver = nullptr;
}

void ProjectTreeItem::deletePermanently()
{
    if (VersionControlTreeItem *vcsTreeItem = this->findChildOfType<VersionControlTreeItem>())
    {
        vcsTreeItem->deletePermanentlyFromRemoteRepo();
    }
    else
    {
        // normally, this should never happen
        File localProjectFile(this->getDocument()->getFullPath());
        App::Workspace().unloadProjectById(this->getId());
        localProjectFile.deleteFile();
        
        if (this->recentFilesList != nullptr)
        {
            this->recentFilesList->cleanup();
        }
    }
}


String ProjectTreeItem::getId() const
{
    VersionControlTreeItem *vcsTreeItem =
        this->findChildOfType<VersionControlTreeItem>();

    if (vcsTreeItem != nullptr)
    {
        return vcsTreeItem->getId();
    }

    return {};
}

String ProjectTreeItem::getStats() const
{
    Array<MidiTrackTreeItem *> layerItems(this->findChildrenOfType<MidiTrackTreeItem>());
    
    int numEvents = 0;
    int numLayers = layerItems.size();

    for (int i = 0; i < numLayers; ++i)
    {
        numEvents += layerItems[i]->getSequence()->size();
    }

    return String(TRANS_PLURAL("{x} layers", numLayers) + " " + TRANS("common::and") + " " + TRANS_PLURAL("{x} events", numEvents));
}

Transport &ProjectTreeItem::getTransport() const noexcept
{
    jassert(this->transport);
    return (*this->transport);
}

ProjectInfo *ProjectTreeItem::getProjectInfo() const noexcept
{
    jassert(this->info);
    return this->info;
}

ProjectTimeline *ProjectTreeItem::getTimeline() const noexcept
{
    jassert(this->timeline);
    return this->timeline;
}

HybridRollEditMode &ProjectTreeItem::getEditMode() noexcept
{
    return this->rollEditMode;
}

HybridRoll *ProjectTreeItem::getLastFocusedRoll() const
{
    return this->sequencerLayout->getRoll();
}

Colour ProjectTreeItem::getColour() const noexcept
{
    return Colour(0xffa489ff);
}

Image ProjectTreeItem::getIcon() const noexcept
{
    return Icons::findByName(Icons::project, TREE_LARGE_ICON_HEIGHT);
}

void ProjectTreeItem::showPage()
{
    this->projectSettings->updateContent();
    App::Layout().showPage(this->projectSettings, this);
}

void ProjectTreeItem::safeRename(const String &newName)
{
    if (newName == this->getName())
    {
        return;
    }
    
    // notify recent files list
    if (this->recentFilesList != nullptr)
    {
        this->recentFilesList->removeById(this->getId());
    }

    this->name = newName;
    
    this->getDocument()->renameFile(newName);
    this->broadcastChangeProjectInfo(this->info);

    // notify recent files list
    if (this->recentFilesList != nullptr)
    {
        this->recentFilesList->
        onProjectStateChanged(this->getName(),
                              this->getDocument()->getFullPath(),
                              this->getId(),
                              true);
    }

    this->dispatchChangeTreeItemView();
}

void ProjectTreeItem::recreatePage()
{
    ValueTree layoutState(Serialization::UI::sequencer);
    if (this->sequencerLayout || this->projectSettings)
    {
        layoutState = this->sequencerLayout->serialize();
    }
    
    this->sequencerLayout = new SequencerLayout(*this);
    
    if (App::isRunningOnPhone())
    {
        this->projectSettings = new ProjectPagePhone(*this);
    }
    else
    {
        this->projectSettings = new ProjectPageDefault(*this);
    }
    
    this->broadcastChangeProjectBeatRange(); // let rolls update themselves
    this->sequencerLayout->deserialize(layoutState);
}

void ProjectTreeItem::showPatternEditor(WeakReference<TreeItem> source)
{
    jassert(source != nullptr);
    this->sequencerLayout->showPatternEditor();
    App::Layout().showPage(this->sequencerLayout, source);
}

void ProjectTreeItem::showLinearEditor(WeakReference<MidiTrack> activeTrack, WeakReference<TreeItem> source)
{
    jassert(source != nullptr);
    jassert(activeTrack != nullptr);

    if (PianoTrackTreeItem *pianoTrack = dynamic_cast<PianoTrackTreeItem *>(activeTrack.get()))
    {
        Array<WeakReference<MidiTrack>> activeTracks;
        activeTracks.addArray(this->findChildrenOfType<PianoTrackTreeItem>(true));
        this->sequencerLayout->showLinearEditor(activeTracks, activeTrack);
        this->lastShownTrack = source;
        App::Layout().showPage(this->sequencerLayout, source);
    }
}

void ProjectTreeItem::hideEditor(WeakReference<MidiTrack> activeTrack, WeakReference<TreeItem> source)
{
    jassert(source != nullptr);
    jassert(activeTrack != nullptr);
    // TODO hide all automation editors if any
}

WeakReference<TreeItem> ProjectTreeItem::getLastShownTrack() const noexcept
{
    return this->lastShownTrack;
}

void ProjectTreeItem::updateActiveGroupEditors()
{
    Array<TrackGroupTreeItem *> myGroups(this->findChildrenOfType<TrackGroupTreeItem>());

    for (int i = 0; i < myGroups.size(); ++i)
    {
        TrackGroupTreeItem *group = myGroups.getUnchecked(i);

        if (group->isMarkerVisible())
        {
            group->showPage();
            return;
        }
    }
}

void ProjectTreeItem::activateLayer(MidiSequence* sequence, bool selectOthers, bool deselectOthers)
{
    if (selectOthers)
    {
        if (PianoTrackTreeItem *item =
            this->findTrackById<PianoTrackTreeItem>(sequence->getTrackId()))
        {
            PianoTrackTreeItem::selectAllPianoSiblings(item);
        }
    }
    else
    {
        if (PianoTrackTreeItem *item =
            this->findTrackById<PianoTrackTreeItem>(sequence->getTrackId()))
        {
            item->setSelected(false, false);
            item->setSelected(true, deselectOthers);
        }
    }
}

//===----------------------------------------------------------------------===//
// Menu
//===----------------------------------------------------------------------===//

bool ProjectTreeItem::hasMenu() const noexcept
{
    return true;
}

ScopedPointer<Component> ProjectTreeItem::createMenu()
{
    return new ProjectCommandPanel(*this, CommandPanel::SlideRight);
}

//===----------------------------------------------------------------------===//
// Dragging
//===----------------------------------------------------------------------===//

bool ProjectTreeItem::isInterestedInDragSource(const DragAndDropTarget::SourceDetails &dragSourceDetails)
{
    return (dragSourceDetails.description == Serialization::Core::track.toString()) ||
           (dragSourceDetails.description == Serialization::Core::trackGroup.toString());
}

//===----------------------------------------------------------------------===//
// Undos
//===----------------------------------------------------------------------===//

UndoStack *ProjectTreeItem::getUndoStack() const noexcept
{
    return this->undoStack.get();
}

void ProjectTreeItem::checkpoint()
{
    this->getUndoStack()->beginNewTransaction({});
}

void ProjectTreeItem::undo()
{
    if (this->getUndoStack()->canUndo())
    {
        this->checkpoint();
        this->getUndoStack()->undo();
    }
}

void ProjectTreeItem::redo()
{
    if (this->getUndoStack()->canRedo())
    {
        this->getUndoStack()->redo();
    }
}

void ProjectTreeItem::clearUndoHistory()
{
    this->getUndoStack()->clearUndoHistory();
}

//===----------------------------------------------------------------------===//
// Project
//===----------------------------------------------------------------------===//

Array<MidiTrack *> ProjectTreeItem::getTracks() const
{
    const ScopedReadLock lock(this->tracksListLock);
    Array<MidiTrack *> tracks;

    // now get all layers inside a tree hierarchy
    this->collectTracks(tracks);
    
    // and explicitly add the only non-tree-owned layers
    tracks.add(this->timeline->getAnnotations());
    tracks.add(this->timeline->getKeySignatures());
    tracks.add(this->timeline->getTimeSignatures());

    return tracks;
}

Array<MidiTrack *> ProjectTreeItem::getSelectedTracks() const
{
    const ScopedReadLock lock(this->tracksListLock);
    Array<MidiTrack *> tracks;
    this->collectTracks(tracks, true);
    return tracks;
}

void ProjectTreeItem::collectTracks(Array<MidiTrack *> &resultArray, bool onlySelected /*= false*/) const
{
    const Array<MidiTrackTreeItem *> treeItems =
        this->findChildrenOfType<MidiTrackTreeItem>();
    
    for (int i = 0; i < treeItems.size(); ++i)
    {
        if (treeItems.getUnchecked(i)->isSelected() || !onlySelected)
        {
            resultArray.add(treeItems.getUnchecked(i));
        }
    }
}

Point<float> ProjectTreeItem::getProjectRangeInBeats() const
{
    float lastBeat = -FLT_MAX;
    float firstBeat = FLT_MAX;
    const float defaultNumBeats = DEFAULT_NUM_BARS * BEATS_PER_BAR;

    Array<MidiTrack *> tracks;
    this->collectTracks(tracks);

    for (auto track : tracks)
    {
        const float layerFirstBeat = track->getSequence()->getFirstBeat();
        const float layerLastBeat = track->getSequence()->getLastBeat();
        //Logger::writeToLog(">  " + String(layerFirstBeat) + " : " + String(layerLastBeat));
        firstBeat = jmin(firstBeat, layerFirstBeat);
        lastBeat = jmax(lastBeat, layerLastBeat);

        // TODO also take into account patterns!!!!!!!
    }
    
    if (firstBeat == FLT_MAX)
    {
        firstBeat = 0;
    }
    else if (firstBeat > lastBeat)
    {
        firstBeat = lastBeat - defaultNumBeats;
    }
    
    if ((lastBeat - firstBeat) < defaultNumBeats)
    {
        lastBeat = firstBeat + defaultNumBeats;
    }

    //Logger::writeToLog(">> " + String(firstBeat) + " : " + String(lastBeat));

    return Point<float>(firstBeat, lastBeat);
}


//===----------------------------------------------------------------------===//
// Serializable
//===----------------------------------------------------------------------===//

ValueTree ProjectTreeItem::serialize() const
{
    this->getDocument()->save();

    ValueTree tree(Serialization::Core::treeItem);
    tree.setProperty(Serialization::Core::treeItemType, this->type, nullptr);
    tree.setProperty(Serialization::Core::filePath, this->getDocument()->getFullPath(), nullptr);
    return tree;
}

void ProjectTreeItem::deserialize(const ValueTree &tree)
{
    this->reset();

    const File fullPathFile = File(tree.getProperty(Serialization::Core::filePath));
    const File relativePathFile = DocumentHelpers::getDocumentSlot(fullPathFile.getFileName());
    
    if (!fullPathFile.existsAsFile() && !relativePathFile.existsAsFile())
    {
        delete this;
        return;
    }
    
    this->getDocument()->load(fullPathFile, relativePathFile);
}

void ProjectTreeItem::reset()
{
    this->transport->seekToPosition(0.f);
    this->vcsItems.clear();
    this->vcsItems.add(this->info);
    this->vcsItems.add(this->timeline);
    this->undoStack->clearUndoHistory();
    TreeItem::reset();
    // TODO test if we need that really (I suggest we don't):
    //this->broadcastReloadProjectContent();
    //this->broadcastChangeProjectBeatRange();
}

ValueTree ProjectTreeItem::save() const
{
    ValueTree tree(Serialization::Core::project);

    tree.setProperty(Serialization::Core::treeItemName, this->name, nullptr);

    tree.appendChild(this->info->serialize(), nullptr);
    tree.appendChild(this->timeline->serialize(), nullptr);
    tree.appendChild(this->undoStack->serialize(), nullptr);
    tree.appendChild(this->transport->serialize(), nullptr);
    tree.appendChild(this->sequencerLayout->serialize(), nullptr);

    TreeItemChildrenSerializer::serializeChildren(*this, tree);

    return tree;
}

void ProjectTreeItem::load(const ValueTree &tree)
{
    this->reset();

    const auto root = tree.hasType(Serialization::Core::project) ?
        tree : tree.getChildWithName(Serialization::Core::project);

    if (!root.isValid()) { return; }

    this->info->deserialize(root);
    this->timeline->deserialize(root);

    // Proceed with basic properties and children
    TreeItem::deserialize(root);

    // Legacy support: if no pattern set manager found, create one
    if (nullptr == this->findChildOfType<PatternEditorTreeItem>())
    {
        // Try to place it after 'Versions' (presumably, index 1)
        this->addChildTreeItem(new PatternEditorTreeItem(), 1);
    }

    this->broadcastReloadProjectContent();
    const auto range = this->broadcastChangeProjectBeatRange();

    // a hack to add some margin to project beat range,
    // then to round beats to nearest bars
    // because rolls' view ranges are rounded to bars
    const float r = float(BEATS_PER_BAR);
    const float viewStartWithMargin = range.getX() - r;
    const float viewEndWithMargin = range.getY() + r;
    const float viewFirstBeat = floorf(viewStartWithMargin / r) * r;
    const float viewLastBeat = ceilf(viewEndWithMargin / r) * r;
    this->broadcastChangeViewBeatRange(viewFirstBeat, viewLastBeat);

    this->undoStack->deserialize(root);

    // At least, when all tracks are ready:
    this->transport->deserialize(root);
    this->sequencerLayout->deserialize(root);
}

void ProjectTreeItem::importMidi(File &file)
{
    MidiFile tempFile;
    ScopedPointer<InputStream> in(new FileInputStream(file));
    const bool readOk = tempFile.readFrom(*in);
    
    if (!readOk)
    {
        DBG("Midi file appears corrupted");
        return;
    }
    
    if (tempFile.getTimeFormat() <= 0)
    {
        DBG("SMPTE format timing is not yet supported");
        return;
    }
    
    for (int trackNum = 0; trackNum < tempFile.getNumTracks(); trackNum++)
    {
        const MidiMessageSequence *currentTrack = tempFile.getTrack(trackNum);
        const String trackName = "Track " + String(trackNum);
        MidiTrackTreeItem *layer = new PianoTrackTreeItem(trackName);
        this->addChildTreeItem(layer);
        layer->importMidi(*currentTrack);
    }
    
    this->broadcastReloadProjectContent();
    this->broadcastChangeProjectBeatRange();
    this->getDocument()->save();
}

//===----------------------------------------------------------------------===//
// ProjectListeners management
//===----------------------------------------------------------------------===//

void ProjectTreeItem::addListener(ProjectListener *listener)
{
    jassert(MessageManager::getInstance()->currentThreadHasLockedMessageManager());
    this->changeListeners.add(listener);
}

void ProjectTreeItem::removeListener(ProjectListener *listener)
{
    jassert(MessageManager::getInstance()->currentThreadHasLockedMessageManager());
    this->changeListeners.remove(listener);
}

void ProjectTreeItem::removeAllListeners()
{
    jassert(MessageManager::getInstance()->currentThreadHasLockedMessageManager());
    this->changeListeners.clear();
}


//===----------------------------------------------------------------------===//
// Broadcaster
//===----------------------------------------------------------------------===//

void ProjectTreeItem::broadcastChangeEvent(const MidiEvent &oldEvent, const MidiEvent &newEvent)
{
    //jassert(oldEvent.isValid()); // old event is allowed to be un-owned
    jassert(newEvent.isValid());
    this->changeListeners.call(&ProjectListener::onChangeMidiEvent, oldEvent, newEvent);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastAddEvent(const MidiEvent &event)
{
    jassert(event.isValid());
    this->changeListeners.call(&ProjectListener::onAddMidiEvent, event);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastRemoveEvent(const MidiEvent &event)
{
    jassert(event.isValid());
    this->changeListeners.call(&ProjectListener::onRemoveMidiEvent, event);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastPostRemoveEvent(MidiSequence *const layer)
{
    this->changeListeners.call(&ProjectListener::onPostRemoveMidiEvent, layer);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastAddTrack(MidiTrack *const track)
{
    this->isLayersHashOutdated = true;

    if (VCS::TrackedItem *tracked = dynamic_cast<VCS::TrackedItem *>(track))
    {
        const ScopedWriteLock lock(this->vcsInfoLock);
        this->vcsItems.addIfNotAlreadyThere(tracked);
    }

    this->changeListeners.call(&ProjectListener::onAddTrack, track);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastRemoveTrack(MidiTrack *const track)
{
    this->isLayersHashOutdated = true;

    if (VCS::TrackedItem *tracked = dynamic_cast<VCS::TrackedItem *>(track))
    {
        const ScopedWriteLock lock(this->vcsInfoLock);
        this->vcsItems.removeAllInstancesOf(tracked);
    }

    this->changeListeners.call(&ProjectListener::onRemoveTrack, track);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastChangeTrackProperties(MidiTrack *const track)
{
    this->changeListeners.call(&ProjectListener::onChangeTrackProperties, track);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastAddClip(const Clip &clip)
{
    this->changeListeners.call(&ProjectListener::onAddClip, clip);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastChangeClip(const Clip &oldClip, const Clip &newClip)
{
    this->changeListeners.call(&ProjectListener::onChangeClip, oldClip, newClip);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastRemoveClip(const Clip &clip)
{
    this->changeListeners.call(&ProjectListener::onRemoveClip, clip);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastPostRemoveClip(Pattern *const pattern)
{
    this->changeListeners.call(&ProjectListener::onPostRemoveClip, pattern);
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastChangeProjectInfo(const ProjectInfo *info)
{
    this->changeListeners.call(&ProjectListener::onChangeProjectInfo, info);
    this->sendChangeMessage();
}

Point<float> ProjectTreeItem::broadcastChangeProjectBeatRange()
{
    // FIXME: bottleneck warning (will call collectTracks every time an event changes):
    // TODO cache current track list
    const Point<float> &beatRange = this->getProjectRangeInBeats();

    const float &firstBeat = beatRange.getX();
    const float &lastBeat = beatRange.getY();
    
    // грязный хак %(
    // changeListeners.call итерирует список от конца к началу
    // и транспорт обновляет позицию индикатора позже, чем роллы
    // и, если ролл ресайзится, индикатор дергается
    // пусть лучше транспорт обновит индикатор 2 раза, но гарантированно перед остальными
    this->transport->onChangeProjectBeatRange(firstBeat, lastBeat);
    
    this->changeListeners.call(&ProjectListener::onChangeProjectBeatRange, firstBeat, lastBeat);
    this->sendChangeMessage();

    return beatRange;
}

void ProjectTreeItem::broadcastReloadProjectContent()
{
    this->changeListeners.call(&ProjectListener::onReloadProjectContent, this->getTracks());
    this->sendChangeMessage();
}

void ProjectTreeItem::broadcastChangeViewBeatRange(float firstBeat, float lastBeat)
{
    this->changeListeners.call(&ProjectListener::onChangeViewBeatRange, firstBeat, lastBeat);
    // this->sendChangeMessage(); the project itself didn't change, so dont call this
}


//===----------------------------------------------------------------------===//
// DocumentOwner
//===----------------------------------------------------------------------===//

bool ProjectTreeItem::onDocumentLoad(File &file)
{
    if (file.existsAsFile())
    {
        const ValueTree tree(DocumentHelpers::load(file));
        if (tree.isValid())
        {
            this->load(tree);
            return true;
        }
    }

    return false;
}

void ProjectTreeItem::onDocumentDidLoad(File &file)
{
    if (this->recentFilesList != nullptr)
    {
        this->recentFilesList->
        onProjectStateChanged(this->getName(),
                              this->getDocument()->getFullPath(),
                              this->getId(),
                              true);
    }
}

bool ProjectTreeItem::onDocumentSave(File &file)
{
    const auto projectNode(this->save());
    // Debug:
    DocumentHelpers::save<XmlSerializer>(file.withFileExtension("xml"), projectNode);
    return DocumentHelpers::save<BinarySerializer>(file, projectNode);
}

void ProjectTreeItem::onDocumentImport(File &file)
{
    if (file.hasFileExtension("mid") || file.hasFileExtension("midi"))
    {
        this->importMidi(file);
    }
}

bool ProjectTreeItem::onDocumentExport(File &file)
{
    if (file.hasFileExtension("mid") || file.hasFileExtension("midi"))
    {
        this->exportMidi(file);
        return true;
    }

    return false;
}

void ProjectTreeItem::exportMidi(File &file) const
{
    MidiFile tempFile;
    tempFile.setTicksPerQuarterNote(int(MS_PER_BEAT));
    
    const auto &tracks = this->getTracks();
    
    for (auto track : tracks)
    {
        // TODO patterns!
        tempFile.addTrack(track->getSequence()->exportMidi());
    }
    
    ScopedPointer<OutputStream> out(new FileOutputStream(file));
    tempFile.writeTo(*out);
}

//===----------------------------------------------------------------------===//
// MidiTrackSource
//===----------------------------------------------------------------------===//

MidiTrack *ProjectTreeItem::getTrackById(const String &trackId)
{
    Array<MidiTrackTreeItem *> allChildren = this->findChildrenOfType<MidiTrackTreeItem>();

    for (int i = 0; i < allChildren.size(); ++i)
    {
        if (allChildren.getUnchecked(i)->getTrackId().toString() == trackId)
        {
            return allChildren.getUnchecked(i);
        }
    }

    return nullptr;
}

Pattern *ProjectTreeItem::getPatternByTrackId(const String &uuid)
{
    // TODO!
    return nullptr;
}

MidiSequence *ProjectTreeItem::getSequenceByTrackId(const String &trackId)
{
    this->rebuildSequencesHashIfNeeded();
    return this->sequencesHash[trackId].get();
}

//===----------------------------------------------------------------------===//
// VCS::TrackedItemsSource
//===----------------------------------------------------------------------===//

String ProjectTreeItem::getVCSName() const
{
    return this->getName();
}

int ProjectTreeItem::getNumTrackedItems()
{
    const ScopedReadLock lock(this->vcsInfoLock);
    return this->vcsItems.size();
}

VCS::TrackedItem *ProjectTreeItem::getTrackedItem(int index)
{
    const ScopedReadLock lock(this->vcsInfoLock);
    return const_cast<VCS::TrackedItem *>(this->vcsItems[index]);
}

VCS::TrackedItem *ProjectTreeItem::initTrackedItem(const Identifier &type, const Uuid &id)
{
    if (type == Serialization::Core::pianoTrack)
    {
        MidiTrackTreeItem *track = new PianoTrackTreeItem("empty");
        track->setVCSUuid(id);
        this->addChildTreeItem(track);
        return track;
    }
    if (type == Serialization::Core::automationTrack)
    {
        MidiTrackTreeItem *track = new AutomationTrackTreeItem("empty");
        track->setVCSUuid(id);
        this->addChildTreeItem(track);
        return track;
    }
    else if (type == Serialization::Core::projectInfo)
    {
        this->info->setVCSUuid(id);
        return this->info;
    }
    else if (type == Serialization::Core::projectTimeline)
    {
        this->timeline->setVCSUuid(id);
        return this->timeline;
    }
    
    return nullptr;
}

bool ProjectTreeItem::deleteTrackedItem(VCS::TrackedItem *item)
{
    if (dynamic_cast<MidiTrackTreeItem *>(item))
    {
        delete item; // will call broadcastRemoveTrack in destructor
        return true;
    }

    return false;
}

void ProjectTreeItem::onResetState()
{
    this->broadcastReloadProjectContent();
    this->broadcastChangeProjectBeatRange();
}

//===----------------------------------------------------------------------===//
// ChangeListener
//===----------------------------------------------------------------------===//

void ProjectTreeItem::changeListenerCallback(ChangeBroadcaster *source)
{
    if (VersionControl *vcs = dynamic_cast<VersionControl *>(source))
    {
        //Logger::writeToLog("ProjectTreeItem :: vcs changed, saving " + vcs->getParentName());
        DocumentOwner::sendChangeMessage();
        //this->getDocument()->save();
        
        // FIXME! a bug reproduced in iOS when callin forceSave() here:
        // VCS start rebuilding diff after a commit in a separate thread,
        // at the same time main thread saves the project and flushes the VCS pack,
        // and eventually the pack cannot get delta data.
        //this->getDocument()->forceSave();
    }
}

void ProjectTreeItem::rebuildSequencesHashIfNeeded()
{
    if (this->isLayersHashOutdated)
    {
        this->sequencesHash.clear();

        this->sequencesHash[this->timeline->getAnnotations()->getTrackId().toString()] =
            this->timeline->getAnnotations()->getSequence();

        this->sequencesHash[this->timeline->getKeySignatures()->getTrackId().toString()] =
            this->timeline->getKeySignatures()->getSequence();

        this->sequencesHash[this->timeline->getTimeSignatures()->getTrackId().toString()] =
            this->timeline->getTimeSignatures()->getSequence();
        
        const Array<MidiTrack *> children = this->findChildrenOfType<MidiTrack>();
        
        for (int i = 0; i < children.size(); ++i)
        {
            const MidiTrack *track = children.getUnchecked(i);
            this->sequencesHash[track->getTrackId().toString()] = track->getSequence();
        }
        
        this->isLayersHashOutdated = false;
    }
}
