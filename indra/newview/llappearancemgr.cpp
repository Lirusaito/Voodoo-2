/** 
 * @file llappearancemgr.cpp
 * @brief Manager for initiating appearance changes on the viewer
 *
 * $LicenseInfo:firstyear=2004&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * 
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 * 
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * 
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llagent.h"
#include "llagentcamera.h"
#include "llagentwearables.h"
#include "llappearancemgr.h"
#include "llattachmentsmgr.h"
#include "llcommandhandler.h"
#include "lleventtimer.h"
#include "llgesturemgr.h"
#include "llinventorybridge.h"
#include "llinventoryfunctions.h"
#include "llinventoryobserver.h"
#include "llnotificationsutil.h"
#include "lloutfitobserver.h"
//#include "lloutfitslist.h"
#include "llselectmgr.h"
//#include "llsidepanelappearance.h"
#include "llviewerobjectlist.h"
#include "llvoavatar.h"
#include "llvoavatarself.h"
#include "llviewerregion.h"
#include "llwearablelist.h"
#include "llinventorypanel.h"
// [RLVa:KB] - Checked: 2011-05-22 (RLVa-1.3.1a)
#include "rlvhandler.h"
#include "rlvhelper.h"
#include "rlvlocks.h"
// [/RLVa:KB]

std::string self_av_string()
{
	return gAgentAvatarp->avString();
}

// RAII thingy to guarantee that a variable gets reset when the Setter
// goes out of scope.  More general utility would be handy - TODO:
// check boost.
class BoolSetter
{
public:
	BoolSetter(bool& var):
		mVar(var)
	{
		mVar = true;
	}
	~BoolSetter()
	{
		mVar = false; 
	}
private:
	bool& mVar;
};

char ORDER_NUMBER_SEPARATOR('@');

class LLOutfitUnLockTimer: public LLEventTimer
{
public:
	LLOutfitUnLockTimer(F32 period) : LLEventTimer(period)
	{
		// restart timer on BOF changed event
		LLOutfitObserver::instance().addBOFChangedCallback(boost::bind(
				&LLOutfitUnLockTimer::reset, this));
		stop();
	}

	/*virtual*/
	BOOL tick()
	{
		if(mEventTimer.hasExpired())
		{
			LLAppearanceMgr::instance().setOutfitLocked(false);
		}
		return FALSE;
	}
	void stop() { mEventTimer.stop(); }
	void start() { mEventTimer.start(); }
	void reset() { mEventTimer.reset(); }
	BOOL getStarted() { return mEventTimer.getStarted(); }

	LLTimer&  getEventTimer() { return mEventTimer;}
};

// support for secondlife:///app/appearance SLapps
/*class LLAppearanceHandler : public LLCommandHandler
{
public:
	// requests will be throttled from a non-trusted browser
	LLAppearanceHandler() : LLCommandHandler("appearance", UNTRUSTED_THROTTLE) {}

	bool handle(const LLSD& params, const LLSD& query_map, LLMediaCtrl* web)
	{
		// support secondlife:///app/appearance/show, but for now we just
		// make all secondlife:///app/appearance SLapps behave this way
		if (!LLUI::sSettingGroups["config"]->getBOOL("EnableAppearance"))
		{
			LLNotificationsUtil::add("NoAppearance", LLSD(), LLSD(), std::string("SwitchToStandardSkinAndQuit"));
			return true;
		}

		LLFloaterSidePanelContainer::showPanel("appearance", LLSD());
		return true;
	}
};

LLAppearanceHandler gAppearanceHandler;*/


LLUUID findDescendentCategoryIDByName(const LLUUID& parent_id, const std::string& name)
{
	LLInventoryModel::cat_array_t cat_array;
	LLInventoryModel::item_array_t item_array;
	LLNameCategoryCollector has_name(name);
	gInventory.collectDescendentsIf(parent_id,
									cat_array,
									item_array,
									LLInventoryModel::EXCLUDE_TRASH,
									has_name);
	if (0 == cat_array.count())
		return LLUUID();
	else
	{
		LLViewerInventoryCategory *cat = cat_array.get(0);
		if (cat)
			return cat->getUUID();
		else
		{
			llwarns << "null cat" << llendl;
			return LLUUID();
		}
	}
}

class LLWearInventoryCategoryCallback : public LLInventoryCallback
{
public:
	LLWearInventoryCategoryCallback(const LLUUID& cat_id, bool append)
	{
		mCatID = cat_id;
		mAppend = append;

		LL_INFOS("Avatar") << self_av_string() << "starting" << LL_ENDL;
	}
	void fire(const LLUUID& item_id)
	{
		/*
		 * Do nothing.  We only care about the destructor
		 *
		 * The reason for this is that this callback is used in a hack where the
		 * same callback is given to dozens of items, and the destructor is called
		 * after the last item has fired the event and dereferenced it -- if all
		 * the events actually fire!
		 */
		LL_DEBUGS("Avatar") << self_av_string() << " fired on copied item, id " << item_id << LL_ENDL;
	}

protected:
	~LLWearInventoryCategoryCallback()
	{
		LL_INFOS("Avatar") << self_av_string() << "done all inventory callbacks" << LL_ENDL;
		
		// Is the destructor called by ordinary dereference, or because the app's shutting down?
		// If the inventory callback manager goes away, we're shutting down, no longer want the callback.
		if( LLInventoryCallbackManager::is_instantiated() )
		{
			LLAppearanceMgr::instance().wearInventoryCategoryOnAvatar(gInventory.getCategory(mCatID), mAppend);
		}
		else
		{
			llwarns << self_av_string() << "Dropping unhandled LLWearInventoryCategoryCallback" << llendl;
		}
	}

private:
	LLUUID mCatID;
	bool mAppend;
};


//Inventory callback updating "dirty" state when destroyed
class LLUpdateDirtyState: public LLInventoryCallback
{
public:
	LLUpdateDirtyState() {}
	virtual ~LLUpdateDirtyState()
	{
		if (LLAppearanceMgr::instanceExists())
		{
			LLAppearanceMgr::getInstance()->updateIsDirty();
		}
	}
	virtual void fire(const LLUUID&) {}
};


LLUpdateAppearanceOnDestroy::LLUpdateAppearanceOnDestroy(bool update_base_outfit_ordering):
	mFireCount(0),
	mUpdateBaseOrder(update_base_outfit_ordering)
{
}

LLUpdateAppearanceOnDestroy::~LLUpdateAppearanceOnDestroy()
{
	LL_INFOS("Avatar") << self_av_string() << "done update appearance on destroy" << LL_ENDL;
	llinfos << "done update appearance on destroy" << llendl;
	
	if (!LLApp::isExiting())
	{
		LLAppearanceMgr::instance().updateAppearanceFromCOF(mUpdateBaseOrder);
	}
}

void LLUpdateAppearanceOnDestroy::fire(const LLUUID& inv_item)
{
	LLViewerInventoryItem* item = (LLViewerInventoryItem*)gInventory.getItem(inv_item);
	const std::string item_name = item ? item->getName() : "ITEM NOT FOUND";
#ifndef LL_RELEASE_FOR_DOWNLOAD
	LL_DEBUGS("Avatar") << self_av_string() << "callback fired [ name:" << item_name << " UUID:" << inv_item << " count:" << mFireCount << " ] " << LL_ENDL;
#endif
	mFireCount++;
}

struct LLFoundData
{
	LLFoundData() :
		mAssetType(LLAssetType::AT_NONE),
		mWearableType(LLWearableType::WT_INVALID),
		mWearable(NULL) {}

	LLFoundData(const LLUUID& item_id,
				const LLUUID& asset_id,
				const std::string& name,
				const LLAssetType::EType& asset_type,
				const LLWearableType::EType& wearable_type,
				const bool is_replacement = false
		) :
		mItemID(item_id),
		mAssetID(asset_id),
		mName(name),
		mAssetType(asset_type),
		mWearableType(wearable_type),
		mIsReplacement(is_replacement),
		mWearable( NULL ) {}
	
	LLUUID mItemID;
	LLUUID mAssetID;
	std::string mName;
	LLAssetType::EType mAssetType;
	LLWearableType::EType mWearableType;
	LLWearable* mWearable;
	bool mIsReplacement;
};

	
class LLWearableHoldingPattern
{
	LOG_CLASS(LLWearableHoldingPattern);

public:
	LLWearableHoldingPattern();
	~LLWearableHoldingPattern();

	bool pollFetchCompletion();
	void onFetchCompletion();
	bool isFetchCompleted();
	bool isTimedOut();

	void checkMissingWearables();
	bool pollMissingWearables();
	bool isMissingCompleted();
	void recoverMissingWearable(LLWearableType::EType type);
	void clearCOFLinksForMissingWearables();
	
	void onWearableAssetFetch(LLWearable *wearable);
	void onAllComplete();

// [SL:KB] - Patch: Appearance-COFCorruption | Checked: 2010-04-14 (Catznip-3.0.0a) | Added: Catznip-2.0.0a
	bool pollStopped();
// [/SL:KB]

	typedef std::list<LLFoundData> found_list_t;
	found_list_t& getFoundList();
	void eraseTypeToLink(LLWearableType::EType type);
	void eraseTypeToRecover(LLWearableType::EType type);
//	void setObjItems(const LLInventoryModel::item_array_t& items);
	void setGestItems(const LLInventoryModel::item_array_t& items);
	bool isMostRecent();
	void handleLateArrivals();
	void resetTime(F32 timeout);
	
private:
	found_list_t mFoundList;
//	LLInventoryModel::item_array_t mObjItems;
	LLInventoryModel::item_array_t mGestItems;
	typedef std::set<S32> type_set_t;
	type_set_t mTypesToRecover;
	type_set_t mTypesToLink;
	S32 mResolved;
	LLTimer mWaitTime;
	bool mFired;
	typedef std::set<LLWearableHoldingPattern*> type_set_hp;
	static type_set_hp sActiveHoldingPatterns;
	bool mIsMostRecent;
	std::set<LLWearable*> mLateArrivals;
	bool mIsAllComplete;
};

LLWearableHoldingPattern::type_set_hp LLWearableHoldingPattern::sActiveHoldingPatterns;

LLWearableHoldingPattern::LLWearableHoldingPattern():
	mResolved(0),
	mFired(false),
	mIsMostRecent(true),
	mIsAllComplete(false)
{
	if (sActiveHoldingPatterns.size()>0)
	{
		llinfos << "Creating LLWearableHoldingPattern when "
				<< sActiveHoldingPatterns.size()
				<< " other attempts are active."
				<< " Flagging others as invalid."
				<< llendl;
		for (type_set_hp::iterator it = sActiveHoldingPatterns.begin();
			 it != sActiveHoldingPatterns.end();
			 ++it)
		{
			(*it)->mIsMostRecent = false;
		}
			 
	}
	sActiveHoldingPatterns.insert(this);
}

LLWearableHoldingPattern::~LLWearableHoldingPattern()
{
	sActiveHoldingPatterns.erase(this);
}

bool LLWearableHoldingPattern::isMostRecent()
{
	return mIsMostRecent;
}

LLWearableHoldingPattern::found_list_t& LLWearableHoldingPattern::getFoundList()
{
	return mFoundList;
}

void LLWearableHoldingPattern::eraseTypeToLink(LLWearableType::EType type)
{
	mTypesToLink.erase(type);
}

void LLWearableHoldingPattern::eraseTypeToRecover(LLWearableType::EType type)
{
	mTypesToRecover.erase(type);
}

// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-06-19 (Catznip-3.0.0a) | Added: Catznip-2.1.2a
//void LLWearableHoldingPattern::setObjItems(const LLInventoryModel::item_array_t& items)
//{
//	mObjItems = items;
//}

void LLWearableHoldingPattern::setGestItems(const LLInventoryModel::item_array_t& items)
{
	mGestItems = items;
}

bool LLWearableHoldingPattern::isFetchCompleted()
{
	return (mResolved >= (S32)getFoundList().size()); // have everything we were waiting for?
}

bool LLWearableHoldingPattern::isTimedOut()
{
	return mWaitTime.hasExpired();
}

void LLWearableHoldingPattern::checkMissingWearables()
{
	if (!isMostRecent())
	{
		// runway why don't we actually skip here?
		llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;
	}
		
	std::vector<S32> found_by_type(LLWearableType::WT_COUNT,0);
	std::vector<S32> requested_by_type(LLWearableType::WT_COUNT,0);
	for (found_list_t::iterator it = getFoundList().begin(); it != getFoundList().end(); ++it)
	{
		LLFoundData &data = *it;
		if (data.mWearableType < LLWearableType::WT_COUNT)
			requested_by_type[data.mWearableType]++;
		if (data.mWearable)
			found_by_type[data.mWearableType]++;
	}

	for (S32 type = 0; type < LLWearableType::WT_COUNT; ++type)
	{
		if (requested_by_type[type] > found_by_type[type])
		{
			llwarns << self_av_string() << "got fewer wearables than requested, type " << type << ": requested " << requested_by_type[type] << ", found " << found_by_type[type] << llendl;
		}
		if (found_by_type[type] > 0)
			continue;
		if (
			// If at least one wearable of certain types (pants/shirt/skirt)
			// was requested but none was found, create a default asset as a replacement.
			// In all other cases, don't do anything.
			// For critical types (shape/hair/skin/eyes), this will keep the avatar as a cloud 
			// due to logic in LLVOAvatarSelf::getIsCloud().
			// For non-critical types (tatoo, socks, etc.) the wearable will just be missing.
			(requested_by_type[type] > 0) &&  
			((type == LLWearableType::WT_PANTS) || (type == LLWearableType::WT_SHIRT) || (type == LLWearableType::WT_SKIRT)))
		{
			mTypesToRecover.insert(type);
			mTypesToLink.insert(type);
			recoverMissingWearable((LLWearableType::EType)type);
			llwarns << self_av_string() << "need to replace " << type << llendl; 
		}
	}

	resetTime(60.0F);
	if (!pollMissingWearables())
	{
		doOnIdleRepeating(boost::bind(&LLWearableHoldingPattern::pollMissingWearables,this));
	}
}

void LLWearableHoldingPattern::onAllComplete()
{
	if (isAgentAvatarValid())
	{
		gAgentAvatarp->outputRezTiming("Agent wearables fetch complete");
	}

	if (!isMostRecent())
	{
		// runway need to skip here?
		llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;
	}

	// Activate all gestures in this folder
	if (mGestItems.count() > 0)
	{
		LL_DEBUGS("Avatar") << self_av_string() << "Activating " << mGestItems.count() << " gestures" << LL_ENDL;
		
		LLGestureMgr::instance().activateGestures(mGestItems);
		
		// Update the inventory item labels to reflect the fact
		// they are active.
		LLViewerInventoryCategory* catp =
			gInventory.getCategory(LLAppearanceMgr::instance().getCOF());
		
		if (catp)
		{
			gInventory.updateCategory(catp);
			gInventory.notifyObservers();
		}
	}

	// Update wearables.
	LL_INFOS("Avatar") << self_av_string() << "Updating agent wearables with " << mResolved << " wearable items " << LL_ENDL;
	LLAppearanceMgr::instance().updateAgentWearables(this, false);
	
// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-03-22 (Catznip-3.0.0a) | Added: Catznip-2.1.2a
//	// Update attachments to match those requested.
//	if (isAgentAvatarValid())
//	{
//		llinfos << "Updating " << mObjItems.count() << " attachments" << llendl;
//		LLAgentWearables::userUpdateAttachments(mObjItems);
//	}

	if (isFetchCompleted() && isMissingCompleted())
	{
		// Only safe to delete if all wearable callbacks and all missing wearables completed.
		delete this;
	}
	else
	{
		mIsAllComplete = true;
		handleLateArrivals();
	}
}

void LLWearableHoldingPattern::onFetchCompletion()
{
	if (!isMostRecent())
	{
		// runway skip here?
		llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;
	}

	checkMissingWearables();
}

// Runs as an idle callback until all wearables are fetched (or we time out).
bool LLWearableHoldingPattern::pollFetchCompletion()
{
	if (!isMostRecent())
	{
		// runway skip here?
		llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;

// [SL:KB] - Patch: Appearance-COFCorruption | Checked: 2010-04-14 (Catznip-3.0.0a) | Added: Catznip-2.0.0a
		// If we were signalled to stop then we shouldn't do anything else except poll for when it's safe to delete ourselves
		doOnIdleRepeating(boost::bind(&LLWearableHoldingPattern::pollStopped, this));
		return true;
// [/SL:KB]
	}

	bool completed = isFetchCompleted();
	bool timed_out = isTimedOut();
	bool done = completed || timed_out;

	if (done)
	{
		LL_INFOS("Avatar") << self_av_string() << "polling, done status: " << completed << " timed out " << timed_out
				<< " elapsed " << mWaitTime.getElapsedTimeF32() << LL_ENDL;

		mFired = true;
		
		if (timed_out)
		{
			llwarns << self_av_string() << "Exceeded max wait time for wearables, updating appearance based on what has arrived" << llendl;
		}

		onFetchCompletion();
	}
	return done;
}

class RecoveredItemLinkCB: public LLInventoryCallback
{
public:
	RecoveredItemLinkCB(LLWearableType::EType type, LLWearable *wearable, LLWearableHoldingPattern* holder):
		mHolder(holder),
		mWearable(wearable),
		mType(type)
	{
	}
	void fire(const LLUUID& item_id)
	{
		if (!mHolder->isMostRecent())
		{
			llwarns << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;
		}

		llinfos << "Recovered item link for type " << mType << llendl;
		mHolder->eraseTypeToLink(mType);
		// Add wearable to FoundData for actual wearing
		LLViewerInventoryItem *item = gInventory.getItem(item_id);
		LLViewerInventoryItem *linked_item = item ? item->getLinkedItem() : NULL;

		if (linked_item)
		{
			gInventory.addChangedMask(LLInventoryObserver::LABEL, linked_item->getUUID());
			
			if (item)
			{
				LLFoundData found(linked_item->getUUID(),
								  linked_item->getAssetUUID(),
								  linked_item->getName(),
								  linked_item->getType(),
								  linked_item->isWearableType() ? linked_item->getWearableType() : LLWearableType::WT_INVALID,
								  true // is replacement
					);
				found.mWearable = mWearable;
				mHolder->getFoundList().push_front(found);
			}
			else
			{
				llwarns << self_av_string() << "inventory item not found for recovered wearable" << llendl;
			}
		}
		else
		{
			llwarns << self_av_string() << "inventory link not found for recovered wearable" << llendl;
		}
	}
private:
	LLWearableHoldingPattern* mHolder;
	LLWearable *mWearable;
	LLWearableType::EType mType;
};

class RecoveredItemCB: public LLInventoryCallback
{
public:
	RecoveredItemCB(LLWearableType::EType type, LLWearable *wearable, LLWearableHoldingPattern* holder):
		mHolder(holder),
		mWearable(wearable),
		mType(type)
	{
	}
	void fire(const LLUUID& item_id)
	{
		if (!mHolder->isMostRecent())
		{
			// runway skip here?
			llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;

// [SL:KB] - Patch: Appearance-COFCorruption | Checked: 2010-04-14 (Catznip-3.0.0a) | Added: Catznip-2.0.0a
			// If we were signalled to stop then we shouldn't do anything else except poll for when it's safe to delete ourselves
			return;
// [/SL:KB]
		}

		LL_DEBUGS("Avatar") << self_av_string() << "Recovered item for type " << mType << LL_ENDL;
		LLViewerInventoryItem *itemp = gInventory.getItem(item_id);
		mWearable->setItemID(item_id);
		LLPointer<LLInventoryCallback> cb = new RecoveredItemLinkCB(mType,mWearable,mHolder);
		mHolder->eraseTypeToRecover(mType);
		llassert(itemp);
		if (itemp)
		{
			link_inventory_item( gAgent.getID(),
					     item_id,
					     LLAppearanceMgr::instance().getCOF(),
					     itemp->getName(),
						 itemp->getDescription(),
					     LLAssetType::AT_LINK,
					     cb);
		}
	}
private:
	LLWearableHoldingPattern* mHolder;
	LLWearable *mWearable;
	LLWearableType::EType mType;
};

void LLWearableHoldingPattern::recoverMissingWearable(LLWearableType::EType type)
{
	if (!isMostRecent())
	{
		// runway skip here?
		llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;
	}
	
		// Try to recover by replacing missing wearable with a new one.
	LLNotificationsUtil::add("ReplacedMissingWearable");
	lldebugs << "Wearable " << LLWearableType::getTypeLabel(type)
			 << " could not be downloaded.  Replaced inventory item with default wearable." << llendl;
	LLWearable* wearable = LLWearableList::instance().createNewWearable(type);

	// Add a new one in the lost and found folder.
	const LLUUID lost_and_found_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_LOST_AND_FOUND);
	LLPointer<LLInventoryCallback> cb = new RecoveredItemCB(type,wearable,this);

	create_inventory_item(gAgent.getID(),
						  gAgent.getSessionID(),
						  lost_and_found_id,
						  wearable->getTransactionID(),
						  wearable->getName(),
						  wearable->getDescription(),
						  wearable->getAssetType(),
						  LLInventoryType::IT_WEARABLE,
						  wearable->getType(),
						  wearable->getPermissions().getMaskNextOwner(),
						  cb);
}

bool LLWearableHoldingPattern::isMissingCompleted()
{
	return mTypesToLink.size()==0 && mTypesToRecover.size()==0;
}

void LLWearableHoldingPattern::clearCOFLinksForMissingWearables()
{
	for (found_list_t::iterator it = getFoundList().begin(); it != getFoundList().end(); ++it)
	{
		LLFoundData &data = *it;
		if ((data.mWearableType < LLWearableType::WT_COUNT) && (!data.mWearable))
		{
			// Wearable link that was never resolved; remove links to it from COF
			LL_INFOS("Avatar") << self_av_string() << "removing link for unresolved item " << data.mItemID.asString() << LL_ENDL;
			LLAppearanceMgr::instance().removeCOFItemLinks(data.mItemID,false);
		}
	}
}

// [SL:KB] - Patch: Appearance-COFCorruption | Checked: 2010-04-14 (Catznip-3.0.0a) | Added: Catznip-2.0.0a
bool LLWearableHoldingPattern::pollStopped()
{
	// We have to keep on polling until we're sure that all callbacks have completed or they'll cause a crash
	if ( (isFetchCompleted()) && (isMissingCompleted()) )
	{
		delete this;
		return true;
	}
	return false;
}
// [/SL:KB]

bool LLWearableHoldingPattern::pollMissingWearables()
{
	if (!isMostRecent())
	{
		// runway skip here?
		llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;

// [SL:KB] - Patch: Appearance-COFCorruption | Checked: 2010-04-14 (Catznip-3.0.0a) | Added: Catznip-2.0.0a
		// If we were signalled to stop then we shouldn't do anything else except poll for when it's safe to delete ourselves
		doOnIdleRepeating(boost::bind(&LLWearableHoldingPattern::pollStopped, this));
		return true;
// [/SL:KB]
	}
	
	bool timed_out = isTimedOut();
	bool missing_completed = isMissingCompleted();
	bool done = timed_out || missing_completed;

	if (!done)
	{
		LL_INFOS("Avatar") << self_av_string() << "polling missing wearables, waiting for items " << mTypesToRecover.size()
				<< " links " << mTypesToLink.size()
				<< " wearables, timed out " << timed_out
				<< " elapsed " << mWaitTime.getElapsedTimeF32()
				<< " done " << done << LL_ENDL;
	}

	if (done)
	{
		gAgentAvatarp->debugWearablesLoaded();

		// BAP - if we don't call clearCOFLinksForMissingWearables()
		// here, we won't have to add the link back in later if the
		// wearable arrives late.  This is to avoid corruption of
		// wearable ordering info.  Also has the effect of making
		// unworn item links visible in the COF under some
		// circumstances.

		//clearCOFLinksForMissingWearables();
		onAllComplete();
	}
	return done;
}

// Handle wearables that arrived after the timeout period expired.
void LLWearableHoldingPattern::handleLateArrivals()
{
	// Only safe to run if we have previously finished the missing
	// wearables and other processing - otherwise we could be in some
	// intermediate state - but have not been superceded by a later
	// outfit change request.
	if (mLateArrivals.size() == 0)
	{
		// Nothing to process.
		return;
	}
	if (!isMostRecent())
	{
		llwarns << self_av_string() << "Late arrivals not handled - outfit change no longer valid" << llendl;
	}
	if (!mIsAllComplete)
	{
		llwarns << self_av_string() << "Late arrivals not handled - in middle of missing wearables processing" << llendl;
	}

	LL_INFOS("Avatar") << self_av_string() << "Need to handle " << mLateArrivals.size() << " late arriving wearables" << LL_ENDL;

	// Update mFoundList using late-arriving wearables.
	std::set<LLWearableType::EType> replaced_types;
	for (LLWearableHoldingPattern::found_list_t::iterator iter = getFoundList().begin();
		 iter != getFoundList().end(); ++iter)
	{
		LLFoundData& data = *iter;
		for (std::set<LLWearable*>::iterator wear_it = mLateArrivals.begin();
			 wear_it != mLateArrivals.end();
			 ++wear_it)
		{
			LLWearable *wearable = *wear_it;

			if(wearable->getAssetID() == data.mAssetID)
			{
				data.mWearable = wearable;

				replaced_types.insert(data.mWearableType);

				// BAP - if we didn't call
				// clearCOFLinksForMissingWearables() earlier, we
				// don't need to restore the link here.  Fixes
				// wearable ordering problems.

				// LLAppearanceMgr::instance().addCOFItemLink(data.mItemID,false);

				// BAP failing this means inventory or asset server
				// are corrupted in a way we don't handle.
				llassert((data.mWearableType < LLWearableType::WT_COUNT) && (wearable->getType() == data.mWearableType));
				break;
			}
		}
	}

	// Remove COF links for any default wearables previously used to replace the late arrivals.
	// All this pussyfooting around with a while loop and explicit
	// iterator incrementing is to allow removing items from the list
	// without clobbering the iterator we're using to navigate.
	LLWearableHoldingPattern::found_list_t::iterator iter = getFoundList().begin();
	while (iter != getFoundList().end())
	{
		LLFoundData& data = *iter;

		// If an item of this type has recently shown up, removed the corresponding replacement wearable from COF.
		if (data.mWearable && data.mIsReplacement &&
			replaced_types.find(data.mWearableType) != replaced_types.end())
		{
			LLAppearanceMgr::instance().removeCOFItemLinks(data.mItemID,false);
			std::list<LLFoundData>::iterator clobber_ator = iter;
			++iter;
			getFoundList().erase(clobber_ator);
		}
		else
		{
			++iter;
		}
	}

	// Clear contents of late arrivals.
	mLateArrivals.clear();

	// Update appearance based on mFoundList
	LLAppearanceMgr::instance().updateAgentWearables(this, false);
}

void LLWearableHoldingPattern::resetTime(F32 timeout)
{
	mWaitTime.reset();
	mWaitTime.setTimerExpirySec(timeout);
}

void LLWearableHoldingPattern::onWearableAssetFetch(LLWearable *wearable)
{
	if (!isMostRecent())
	{
		llwarns << self_av_string() << "skipping because LLWearableHolding pattern is invalid (superceded by later outfit request)" << llendl;
	}
	
	mResolved += 1;  // just counting callbacks, not successes.
	LL_DEBUGS("Avatar") << self_av_string() << "resolved " << mResolved << "/" << getFoundList().size() << LL_ENDL;
	if (!wearable)
	{
		llwarns << self_av_string() << "no wearable found" << llendl;
	}

	if (mFired)
	{
		llwarns << self_av_string() << "called after holder fired" << llendl;
		if (wearable)
		{
			mLateArrivals.insert(wearable);
			if (mIsAllComplete)
			{
				handleLateArrivals();
			}
		}
		return;
	}

	if (!wearable)
	{
		return;
	}

	for (LLWearableHoldingPattern::found_list_t::iterator iter = getFoundList().begin();
		 iter != getFoundList().end(); ++iter)
	{
		LLFoundData& data = *iter;
		if(wearable->getAssetID() == data.mAssetID)
		{
			// Failing this means inventory or asset server are corrupted in a way we don't handle.
			if ((data.mWearableType >= LLWearableType::WT_COUNT) || (wearable->getType() != data.mWearableType))
			{
				llwarns << self_av_string() << "recovered wearable but type invalid. inventory wearable type: " << data.mWearableType << " asset wearable type: " << wearable->getType() << llendl;
				break;
			}

			data.mWearable = wearable;
		}
	}
}

static void onWearableAssetFetch(LLWearable* wearable, void* data)
{
	LLWearableHoldingPattern* holder = (LLWearableHoldingPattern*)data;
	holder->onWearableAssetFetch(wearable);
}


static void removeDuplicateItems(LLInventoryModel::item_array_t& items)
{
	LLInventoryModel::item_array_t new_items;
	std::set<LLUUID> items_seen;
	std::deque<LLViewerInventoryItem*> tmp_list;
	// Traverse from the front and keep the first of each item
	// encountered, so we actually keep the *last* of each duplicate
	// item.  This is needed to give the right priority when adding
	// duplicate items to an existing outfit.
	for (S32 i=items.count()-1; i>=0; i--)
	{
		LLViewerInventoryItem *item = items.get(i);
		LLUUID item_id = item->getLinkedUUID();
		if (items_seen.find(item_id)!=items_seen.end())
			continue;
		items_seen.insert(item_id);
		tmp_list.push_front(item);
	}
	for (std::deque<LLViewerInventoryItem*>::iterator it = tmp_list.begin();
		 it != tmp_list.end();
		 ++it)
	{
		new_items.put(*it);
	}
	items = new_items;
}

// [SL:KB] - Patch: Appearance-WearableDuplicateAssets | Checked: 2011-07-24 (Catznip-2.6.0e) | Added: Catznip-2.6.0e
static void removeDuplicateWearableItemsByAssetID(LLInventoryModel::item_array_t& items)
{
	std::set<LLUUID> idsAsset;
	for (S32 idxItem = items.count() - 1; idxItem >= 0; idxItem--)
	{
		const LLViewerInventoryItem* pItem = items.get(idxItem);
		if (!pItem->isWearableType())
			continue;
		if (idsAsset.end() == idsAsset.find(pItem->getAssetUUID()))
			idsAsset.insert(pItem->getAssetUUID());
		else
			items.remove(idxItem);
	}
}
// [/SL:KB]

const LLUUID LLAppearanceMgr::getCOF() const
{
	return gInventory.findCategoryUUIDForType(LLFolderType::FT_CURRENT_OUTFIT);
}


const LLViewerInventoryItem* LLAppearanceMgr::getBaseOutfitLink()
{
	const LLUUID& current_outfit_cat = getCOF();
	LLInventoryModel::cat_array_t cat_array;
	LLInventoryModel::item_array_t item_array;
	// Can't search on FT_OUTFIT since links to categories return FT_CATEGORY for type since they don't
	// return preferred type.
	LLIsType is_category( LLAssetType::AT_CATEGORY ); 
	gInventory.collectDescendentsIf(current_outfit_cat,
									cat_array,
									item_array,
									false,
									is_category,
									false);
	for (LLInventoryModel::item_array_t::const_iterator iter = item_array.begin();
		 iter != item_array.end();
		 iter++)
	{
		const LLViewerInventoryItem *item = (*iter);
		const LLViewerInventoryCategory *cat = item->getLinkedCategory();
		if (cat && cat->getPreferredType() == LLFolderType::FT_OUTFIT)
		{
			const LLUUID parent_id = cat->getParentUUID();
			LLViewerInventoryCategory*  parent_cat =  gInventory.getCategory(parent_id);
			// if base outfit moved to trash it means that we don't have base outfit
			if (parent_cat != NULL && parent_cat->getPreferredType() == LLFolderType::FT_TRASH)
			{
				return NULL;
			}
			return item;
		}
	}
	return NULL;
}

bool LLAppearanceMgr::getBaseOutfitName(std::string& name)
{
	const LLViewerInventoryItem* outfit_link = getBaseOutfitLink();
	if(outfit_link)
	{
		const LLViewerInventoryCategory *cat = outfit_link->getLinkedCategory();
		if (cat)
		{
			name = cat->getName();
			return true;
		}
	}
	return false;
}

const LLUUID LLAppearanceMgr::getBaseOutfitUUID()
{
	const LLViewerInventoryItem* outfit_link = getBaseOutfitLink();
	if (!outfit_link || !outfit_link->getIsLinkType()) return LLUUID::null;

	const LLViewerInventoryCategory* outfit_cat = outfit_link->getLinkedCategory();
	if (!outfit_cat) return LLUUID::null;

	if (outfit_cat->getPreferredType() != LLFolderType::FT_OUTFIT)
	{
		llwarns << "Expected outfit type:" << LLFolderType::FT_OUTFIT << " but got type:" << outfit_cat->getType() << " for folder name:" << outfit_cat->getName() << llendl;
		return LLUUID::null;
	}

	return outfit_cat->getUUID();
}

bool LLAppearanceMgr::wearItemOnAvatar(const LLUUID& item_id_to_wear, bool do_update, bool replace, LLPointer<LLInventoryCallback> cb)
{
	if (item_id_to_wear.isNull()) return false;

	// *TODO: issue with multi-wearable should be fixed:
	// in this case this method will be called N times - loading started for each item
	// and than N times will be called - loading completed for each item.
	// That means subscribers will be notified that loading is done after first item in a batch is worn.
	// (loading indicator disappears for example before all selected items are worn)
	// Have not fix this issue for 2.1 because of stability reason. EXT-7777.

	// Disabled for now because it is *not* acceptable to call updateAppearanceFromCOF() multiple times
//	gAgentWearables.notifyLoadingStarted();

	LLViewerInventoryItem* item_to_wear = gInventory.getItem(item_id_to_wear);
	if (!item_to_wear) return false;

	if (gInventory.isObjectDescendentOf(item_to_wear->getUUID(), gInventory.getLibraryRootFolderID()))
	{
		LLPointer<LLInventoryCallback> cb = new WearOnAvatarCallback(replace);
		copy_inventory_item(gAgent.getID(), item_to_wear->getPermissions().getOwner(), item_to_wear->getUUID(), LLUUID::null, std::string(),cb);
		return false;
	} 
	else if (!gInventory.isObjectDescendentOf(item_to_wear->getUUID(), gInventory.getRootFolderID()))
	{
		return false; // not in library and not in agent's inventory
	}
	else if (gInventory.isObjectDescendentOf(item_to_wear->getUUID(), gInventory.findCategoryUUIDForType(LLFolderType::FT_TRASH)))
	{
		LLNotificationsUtil::add("CannotWearTrash");
		return false;
	}
	else if (gInventory.isObjectDescendentOf(item_to_wear->getUUID(), LLAppearanceMgr::instance().getCOF())) // EXT-84911
	{
		return false;
	}

// [RLVa:KB] - Checked: 2010-09-04 (RLVa-1.2.1a) | Modified: RLVa-1.2.1a
	if ( (rlv_handler_t::isEnabled()) && 
		 ((gRlvAttachmentLocks.hasLockedAttachmentPoint(RLV_LOCK_ANY)) || (gRlvWearableLocks.hasLockedWearableType(RLV_LOCK_ANY))) )
	{
		switch (item_to_wear->getType())
		{
			case LLAssetType::AT_BODYPART:
			case LLAssetType::AT_CLOTHING:
				{
					ERlvWearMask eWear = gRlvWearableLocks.canWear(item_to_wear);
					if ( (RLV_WEAR_LOCKED == eWear) || ((replace) && ((RLV_WEAR_REPLACE & eWear) == 0)) )
						return false;
				}
				break;
			case LLAssetType::AT_OBJECT:
				{
					ERlvWearMask eWear = gRlvAttachmentLocks.canAttach(item_to_wear);
					if ( (RLV_WEAR_LOCKED == eWear) || ((replace) && ((RLV_WEAR_REPLACE & eWear) == 0)) )
						return false;
				}
				break;
			default:
				return false;
		}
	}
// [/RLVa:KB]

	switch (item_to_wear->getType())
	{
	case LLAssetType::AT_CLOTHING:
		if (gAgentWearables.areWearablesLoaded())
		{
			S32 wearable_count = gAgentWearables.getWearableCount(item_to_wear->getWearableType());
			if ((replace && wearable_count != 0) ||
				(wearable_count >= LLAgentWearables::MAX_CLOTHING_PER_TYPE) )
			{
				removeCOFItemLinks(gAgentWearables.getWearableItemID(item_to_wear->getWearableType(), wearable_count-1), false);
			}
			addCOFItemLink(item_to_wear, do_update, cb);
		} 
		break;
	case LLAssetType::AT_BODYPART:
		// TODO: investigate wearables may not be loaded at this point EXT-8231
		
		// Remove the existing wearables of the same type.
		// Remove existing body parts anyway because we must not be able to wear e.g. two skins.
		removeCOFLinksOfType(item_to_wear->getWearableType(), false);

		addCOFItemLink(item_to_wear, do_update, cb);
		break;
	case LLAssetType::AT_OBJECT:
		rez_attachment(item_to_wear, NULL, replace);
		break;
	default: return false;;
	}

	return true;
}

// Update appearance from outfit folder.
void LLAppearanceMgr::changeOutfit(bool proceed, const LLUUID& category, bool append)
{
	if (!proceed)
		return;
	LLAppearanceMgr::instance().updateCOF(category,append);
}

void LLAppearanceMgr::replaceCurrentOutfit(const LLUUID& new_outfit)
{
	LLViewerInventoryCategory* cat = gInventory.getCategory(new_outfit);
	wearInventoryCategory(cat, false, false);
}

// Open outfit renaming dialog.
void LLAppearanceMgr::renameOutfit(const LLUUID& outfit_id)
{
	LLViewerInventoryCategory* cat = gInventory.getCategory(outfit_id);
	if (!cat)
	{
		return;
	}

	LLSD args;
	args["NAME"] = cat->getName();

	LLSD payload;
	payload["cat_id"] = outfit_id;

	LLNotificationsUtil::add("RenameOutfit", args, payload, boost::bind(onOutfitRename, _1, _2));
}

// User typed new outfit name.
// static
void LLAppearanceMgr::onOutfitRename(const LLSD& notification, const LLSD& response)
{
	S32 option = LLNotificationsUtil::getSelectedOption(notification, response);
	if (option != 0) return; // canceled

	std::string outfit_name = response["new_name"].asString();
	LLStringUtil::trim(outfit_name);
	if (!outfit_name.empty())
	{
		LLUUID cat_id = notification["payload"]["cat_id"].asUUID();
		rename_category(&gInventory, cat_id, outfit_name);
	}
}

void LLAppearanceMgr::setOutfitLocked(bool locked)
{
	if (mOutfitLocked == locked)
	{
		return;
	}

	mOutfitLocked = locked;
	if (locked)
	{
		mUnlockOutfitTimer->reset();
		mUnlockOutfitTimer->start();
	}
	else
	{
		mUnlockOutfitTimer->stop();
	}

	LLOutfitObserver::instance().notifyOutfitLockChanged();
}

void LLAppearanceMgr::addCategoryToCurrentOutfit(const LLUUID& cat_id)
{
	LLViewerInventoryCategory* cat = gInventory.getCategory(cat_id);
	wearInventoryCategory(cat, false, true);
}

void LLAppearanceMgr::takeOffOutfit(const LLUUID& cat_id)
{
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	LLFindWearablesEx collector(/*is_worn=*/ true, /*include_body_parts=*/ false);

	gInventory.collectDescendentsIf(cat_id, cats, items, FALSE, collector);

	LLInventoryModel::item_array_t::const_iterator it = items.begin();
	const LLInventoryModel::item_array_t::const_iterator it_end = items.end();
	for( ; it_end != it; ++it)
	{
		LLViewerInventoryItem* item = *it;
		removeItemFromAvatar(item->getUUID());
	}
}

// Create a copy of src_id + contents as a subfolder of dst_id.
void LLAppearanceMgr::shallowCopyCategory(const LLUUID& src_id, const LLUUID& dst_id,
											  LLPointer<LLInventoryCallback> cb)
{
	LLInventoryCategory *src_cat = gInventory.getCategory(src_id);
	if (!src_cat)
	{
		llwarns << "folder not found for src " << src_id.asString() << llendl;
		return;
	}
	llinfos << "starting, src_id " << src_id << " name " << src_cat->getName() << " dst_id " << dst_id << llendl;
	LLUUID parent_id = dst_id;
	if(parent_id.isNull())
	{
		parent_id = gInventory.getRootFolderID();
	}
	LLUUID subfolder_id = gInventory.createNewCategory( parent_id,
														LLFolderType::FT_NONE,
														src_cat->getName());
	shallowCopyCategoryContents(src_id, subfolder_id, cb);

	gInventory.notifyObservers();
}

// Copy contents of src_id to dst_id.
void LLAppearanceMgr::shallowCopyCategoryContents(const LLUUID& src_id, const LLUUID& dst_id, LLPointer<LLInventoryCallback> cb)
{
	LLInventoryModel::cat_array_t* cats;
	LLInventoryModel::item_array_t* items;
	gInventory.getDirectDescendentsOf(src_id, cats, items);
	llinfos << "copying " << items->count() << " items" << llendl;
	copyItems(dst_id, items, cb);
}

void LLAppearanceMgr::copyItems(const LLUUID& dst_id, LLInventoryModel::item_array_t* items, LLPointer<LLInventoryCallback> cb)
{
	for (LLInventoryModel::item_array_t::const_iterator iter = items->begin();
		 iter != items->end();
		 ++iter)
	{
		const LLViewerInventoryItem* item = (*iter);
		switch (item->getActualType())
		{
			case LLAssetType::AT_LINK:
			{
				//LLInventoryItem::getDescription() is used for a new description 
				//to propagate ordering information saved in descriptions of links
				link_inventory_item(gAgent.getID(),
									item->getLinkedUUID(),
									dst_id,
									item->getName(),
									item->LLInventoryItem::getDescription(),
									LLAssetType::AT_LINK, cb);
				break;
			}
			case LLAssetType::AT_LINK_FOLDER:
			{
				LLViewerInventoryCategory *catp = item->getLinkedCategory();
				// Skip copying outfit links.
				if (catp && catp->getPreferredType() != LLFolderType::FT_OUTFIT)
				{
					link_inventory_item(gAgent.getID(),
										item->getLinkedUUID(),
										dst_id,
										item->getName(),
										item->getDescription(),
										LLAssetType::AT_LINK_FOLDER, cb);
				}
				break;
			}
			case LLAssetType::AT_CLOTHING:
			case LLAssetType::AT_OBJECT:
			case LLAssetType::AT_BODYPART:
			case LLAssetType::AT_GESTURE:
			{
				if(!item->getPermissions().allowCopyBy(gAgent.getID()))
				{
					link_inventory_item(gAgent.getID(),
										item->getUUID(),
										dst_id,
										item->getName(),
										item->getDescription(),
										LLAssetType::AT_LINK, cb);
				}
				else
				{
					llinfos << "copying inventory item " << item->getName() << llendl;
					copy_inventory_item(gAgent.getID(),
										item->getPermissions().getOwner(),
										item->getUUID(),
										dst_id,
										item->getName(),
										cb);
				}
				break;
			}
			default:
				// Ignore non-outfit asset types
				break;
		}
	}
}

BOOL LLAppearanceMgr::getCanMakeFolderIntoOutfit(const LLUUID& folder_id)
{
	// These are the wearable items that are required for considering this
	// folder as containing a complete outfit.
	U32 required_wearables = 0;
	required_wearables |= 1LL << LLWearableType::WT_SHAPE;
	required_wearables |= 1LL << LLWearableType::WT_SKIN;
	required_wearables |= 1LL << LLWearableType::WT_HAIR;
	required_wearables |= 1LL << LLWearableType::WT_EYES;

	// These are the wearables that the folder actually contains.
	U32 folder_wearables = 0;
	LLInventoryModel::cat_array_t* cats;
	LLInventoryModel::item_array_t* items;
	gInventory.getDirectDescendentsOf(folder_id, cats, items);
	for (LLInventoryModel::item_array_t::const_iterator iter = items->begin();
		 iter != items->end();
		 ++iter)
	{
		const LLViewerInventoryItem* item = (*iter);
		if (item->isWearableType())
		{
			const LLWearableType::EType wearable_type = item->getWearableType();
			folder_wearables |= 1LL << wearable_type;
		}
	}

	// If the folder contains the required wearables, return TRUE.
	return ((required_wearables & folder_wearables) == required_wearables);
}

bool LLAppearanceMgr::getCanRemoveOutfit(const LLUUID& outfit_cat_id)
{
	// Disallow removing the base outfit.
	if (outfit_cat_id == getBaseOutfitUUID())
	{
		return false;
	}

	// Check if the outfit folder itself is removable.
	if (!get_is_category_removable(&gInventory, outfit_cat_id))
	{
		return false;
	}

	// Check for the folder's non-removable descendants.
	LLFindNonRemovableObjects filter_non_removable;
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	LLInventoryModel::item_array_t::const_iterator it;
	gInventory.collectDescendentsIf(outfit_cat_id, cats, items, false, filter_non_removable);
	if (!cats.empty() || !items.empty())
	{
		return false;
	}

	return true;
}

// static
bool LLAppearanceMgr::getCanRemoveFromCOF(const LLUUID& outfit_cat_id)
{
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	LLFindWearablesEx is_worn(/*is_worn=*/ true, /*include_body_parts=*/ false);
	gInventory.collectDescendentsIf(outfit_cat_id,
		cats,
		items,
		LLInventoryModel::EXCLUDE_TRASH,
		is_worn);
	return items.size() > 0;
}

// static
bool LLAppearanceMgr::getCanAddToCOF(const LLUUID& outfit_cat_id)
{
	if (gAgentWearables.isCOFChangeInProgress())
	{
		return false;
	}

	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	LLFindWearablesEx not_worn(/*is_worn=*/ false, /*include_body_parts=*/ false);
	gInventory.collectDescendentsIf(outfit_cat_id,
		cats,
		items,
		LLInventoryModel::EXCLUDE_TRASH,
		not_worn);
	return items.size() > 0;
}

bool LLAppearanceMgr::getCanReplaceCOF(const LLUUID& outfit_cat_id)
{
	// Don't allow wearing anything while we're changing appearance.
	if (gAgentWearables.isCOFChangeInProgress())
	{
		return false;
	}

	// Check whether it's the base outfit.
//	if (outfit_cat_id.isNull() || outfit_cat_id == getBaseOutfitUUID())
// [SL:KB] - Patch: Appearance-Misc | Checked: 2010-09-21 (Catznip-3.0.0a) | Added: Catznip-2.1.2d
	if ( (outfit_cat_id.isNull()) || ((outfit_cat_id == getBaseOutfitUUID()) && (!isOutfitDirty())) )
// [/SL:KB]
	{
		return false;
	}

	// Check whether the outfit contains any wearables we aren't wearing already (STORM-702).
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	LLFindWearablesEx is_worn(/*is_worn=*/ false, /*include_body_parts=*/ true);
	gInventory.collectDescendentsIf(outfit_cat_id,
		cats,
		items,
		LLInventoryModel::EXCLUDE_TRASH,
		is_worn);
	return items.size() > 0;
}

void LLAppearanceMgr::purgeBaseOutfitLink(const LLUUID& category)
{
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	gInventory.collectDescendents(category, cats, items,
								  LLInventoryModel::EXCLUDE_TRASH);
	for (S32 i = 0; i < items.count(); ++i)
	{
		LLViewerInventoryItem *item = items.get(i);
		if (item->getActualType() != LLAssetType::AT_LINK_FOLDER)
			continue;
		if (item->getIsLinkType())
		{
			LLViewerInventoryCategory* catp = item->getLinkedCategory();
			if(catp && catp->getPreferredType() == LLFolderType::FT_OUTFIT)
			{
				gInventory.purgeObject(item->getUUID());
			}
		}
	}
}

void LLAppearanceMgr::purgeCategory(const LLUUID& category, bool keep_outfit_links)
{
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	gInventory.collectDescendents(category, cats, items,
								  LLInventoryModel::EXCLUDE_TRASH);
	for (S32 i = 0; i < items.count(); ++i)
	{
		LLViewerInventoryItem *item = items.get(i);
		if (keep_outfit_links && (item->getActualType() == LLAssetType::AT_LINK_FOLDER))
			continue;
		if (item->getIsLinkType())
		{
			gInventory.purgeObject(item->getUUID());
		}
	}
}

// [SL:KB] - Checked: 2010-04-24 (RLVa-1.2.0f) | Added: RLVa-1.2.0f
void LLAppearanceMgr::syncCOF(const LLInventoryModel::item_array_t& items, LLAssetType::EType type, LLPointer<LLInventoryCallback> cb)
{
	const LLUUID idCOF = getCOF();
	LLInventoryModel::item_array_t cur_cof_items, new_cof_items = items;

	// Grab the current COF contents
	LLIsType f(type);
	LLInventoryModel::cat_array_t cats; 
	gInventory.collectDescendentsIf(getCOF(), cats, cur_cof_items, LLInventoryModel::EXCLUDE_TRASH, f);

	// Purge everything in cur_cof_items that isn't part of new_cof_items
	for (S32 idxCurItem = 0, cntCurItem = cur_cof_items.count(); idxCurItem < cntCurItem; idxCurItem++)
	{
		const LLViewerInventoryItem* pItem = cur_cof_items.get(idxCurItem);
		if (std::find_if(new_cof_items.begin(), new_cof_items.end(), RlvPredIsEqualOrLinkedItem(pItem)) == new_cof_items.end())
		{
			// Item doesn't exist in new_cof_items => purge (if it's a link)
			if (pItem->getIsLinkType())
				gInventory.purgeObject(pItem->getUUID());
		}
		else
		{
			// Item exists in new_cof_items => remove *all* occurances in new_cof_items (removes duplicate COF links to this item as well)
			new_cof_items.erase(
				std::remove_if(new_cof_items.begin(), new_cof_items.end(), RlvPredIsEqualOrLinkedItem(pItem)), new_cof_items.end());
		}
	}

	// Link to whatever remains in new_cof_items
	for (S32 idxNewItem = 0, cntNewItem = new_cof_items.count(); idxNewItem < cntNewItem; idxNewItem++)
	{
		const LLInventoryItem* pItem = new_cof_items.get(idxNewItem);
		link_inventory_item(
			gAgent.getID(), pItem->getLinkedUUID(), idCOF, pItem->getName(), pItem->LLInventoryItem::getDescription(), LLAssetType::AT_LINK, cb);
	}
}
// [/SL:KB]

// Keep the last N wearables of each type.  For viewer 2.0, N is 1 for
// both body parts and clothing items.
void LLAppearanceMgr::filterWearableItems(
	LLInventoryModel::item_array_t& items, S32 max_per_type)
{
	// Divvy items into arrays by wearable type.
	std::vector<LLInventoryModel::item_array_t> items_by_type(LLWearableType::WT_COUNT);
	divvyWearablesByType(items, items_by_type);

	// rebuild items list, retaining the last max_per_type of each array
	items.clear();
	for (S32 i=0; i<LLWearableType::WT_COUNT; i++)
	{
		S32 size = items_by_type[i].size();
		if (size <= 0)
			continue;
//		S32 start_index = llmax(0,size-max_per_type);
// [SL:KB] - Patch: Appearance-Misc | Checked: 2010-05-11 (Catznip-3.0.0a) | Added: Catznip-2.0.0h
		S32 start_index = 
			llmax(0, size - ((LLAssetType::AT_BODYPART == LLWearableType::getAssetType((LLWearableType::EType)i)) ? 1 : max_per_type));
// [/SL:KB[
		for (S32 j = start_index; j<size; j++)
		{
			items.push_back(items_by_type[i][j]);
		}
	}
}

// Create links to all listed items.
void LLAppearanceMgr::linkAll(const LLUUID& cat_uuid,
								  LLInventoryModel::item_array_t& items,
								  LLPointer<LLInventoryCallback> cb)
{
	for (S32 i=0; i<items.count(); i++)
	{
		const LLInventoryItem* item = items.get(i).get();
		link_inventory_item(gAgent.getID(),
							item->getLinkedUUID(),
							cat_uuid,
							item->getName(),
							item->LLInventoryItem::getDescription(),
							LLAssetType::AT_LINK,
							cb);

		const LLViewerInventoryCategory *cat = gInventory.getCategory(cat_uuid);
		const std::string cat_name = cat ? cat->getName() : "CAT NOT FOUND";
#ifndef LL_RELEASE_FOR_DOWNLOAD
		LL_DEBUGS("Avatar") << self_av_string() << "Linking Item [ name:" << item->getName() << " UUID:" << item->getUUID() << " ] to Category [ name:" << cat_name << " UUID:" << cat_uuid << " ] " << LL_ENDL;
#endif
	}
}


//void LLAppearanceMgr::updateCOF(const LLUUID& category, bool append)
// [RLVa:KB] - Checked: 2010-03-05 (RLVa-1.2.0b) | Added: RLVa-1.2.0b
void LLAppearanceMgr::updateCOF(const LLUUID& category, bool append)
{
	LLInventoryModel::item_array_t body_items_new, wear_items_new, obj_items_new, gest_items_new;
	getDescendentsOfAssetType(category, body_items_new, LLAssetType::AT_BODYPART, false);
	getDescendentsOfAssetType(category, wear_items_new, LLAssetType::AT_CLOTHING, false);
	getDescendentsOfAssetType(category, obj_items_new, LLAssetType::AT_OBJECT, false);
	getDescendentsOfAssetType(category, gest_items_new, LLAssetType::AT_GESTURE, false);
	updateCOF(body_items_new, wear_items_new, obj_items_new, gest_items_new, append, category);
}
void LLAppearanceMgr::updateCOF(LLInventoryModel::item_array_t& body_items_new, 
								LLInventoryModel::item_array_t& wear_items_new, 
								LLInventoryModel::item_array_t& obj_items_new,
								LLInventoryModel::item_array_t& gest_items_new,
								bool append /*=false*/, const LLUUID& idOutfit /*=LLUUID::null*/)
// [/RLVa:KB]
{
//	LLViewerInventoryCategory *pcat = gInventory.getCategory(category);
//	llinfos << "starting, cat " << (pcat ? pcat->getName() : "[UNKNOWN]") << llendl;
// [RLVa:KB] - Checked: 2010-03-26 (RLVa-1.2.0b) | Added: RLVa-1.2.0b
	// RELEASE-RLVa: [SL-2.0.0] If pcat ever gets used for anything further down the beta we'll know about it
	llinfos << "starting" << llendl;
// [/RLVa:KB]

	const LLUUID cof = getCOF();

	// Deactivate currently active gestures in the COF, if replacing outfit
	if (!append)
	{
		LLInventoryModel::item_array_t gest_items;
		getDescendentsOfAssetType(cof, gest_items, LLAssetType::AT_GESTURE, false);
		for(S32 i = 0; i  < gest_items.count(); ++i)
		{
			LLViewerInventoryItem *gest_item = gest_items.get(i);
			if ( LLGestureMgr::instance().isGestureActive( gest_item->getLinkedUUID()) )
			{
				LLGestureMgr::instance().deactivateGesture( gest_item->getLinkedUUID() );
			}
		}
	}
	
	// Collect and filter descendents to determine new COF contents.

	// - Body parts: always include COF contents as a fallback in case any
	// required parts are missing.
	// Preserve body parts from COF if appending.
	LLInventoryModel::item_array_t body_items;
	getDescendentsOfAssetType(cof, body_items, LLAssetType::AT_BODYPART, false);
//	getDescendentsOfAssetType(category, body_items, LLAssetType::AT_BODYPART, false);
// [RLVa:KB] - Checked: 2010-03-19 (RLVa-1.2.0c) | Modified: RLVa-1.2.0b
	// Filter out any new body parts that can't be worn before adding them
	if ( (rlv_handler_t::isEnabled()) && (gRlvWearableLocks.hasLockedWearableType(RLV_LOCK_ANY)) )
		body_items_new.erase(std::remove_if(body_items_new.begin(), body_items_new.end(), RlvPredCanNotWearItem(RLV_WEAR_REPLACE)), body_items_new.end());
	body_items.insert(body_items.end(), body_items_new.begin(), body_items_new.end());
// [/RLVa:KB]
	// NOTE-RLVa: we don't actually want to favour COF body parts over the folder's body parts (if only because it breaks force wear)
//	if (append)
//		reverse(body_items.begin(), body_items.end());
	// Reduce body items to max of one per type.
	removeDuplicateItems(body_items);
	filterWearableItems(body_items, 1);

	// - Wearables: include COF contents only if appending.
	LLInventoryModel::item_array_t wear_items;
	if (append)
		getDescendentsOfAssetType(cof, wear_items, LLAssetType::AT_CLOTHING, false);
// [RLVa:KB] - Checked: 2010-03-19 (RLVa-1.2.0c) | Modified: RLVa-1.2.0b
	else if ( (rlv_handler_t::isEnabled()) && (gRlvWearableLocks.hasLockedWearableType(RLV_LOCK_ANY)) )
	{
		// Make sure that all currently locked clothing layers remain in COF when replacing
		getDescendentsOfAssetType(cof, wear_items, LLAssetType::AT_CLOTHING, false);
		wear_items.erase(std::remove_if(wear_items.begin(), wear_items.end(), rlvPredCanRemoveItem), wear_items.end());
	}
// [/RLVa:KB]
//	getDescendentsOfAssetType(category, wear_items, LLAssetType::AT_CLOTHING, false);
// [RLVa:KB] - Checked: 2010-03-19 (RLVa-1.2.0c) | Modified: RLVa-1.2.0b
	// Filter out any new wearables that can't be worn before adding them
	if ( (rlv_handler_t::isEnabled()) && (gRlvWearableLocks.hasLockedWearableType(RLV_LOCK_ANY)) )
		wear_items_new.erase(std::remove_if(wear_items_new.begin(), wear_items_new.end(), RlvPredCanNotWearItem(RLV_WEAR)), wear_items_new.end());
	wear_items.insert(wear_items.end(), wear_items_new.begin(), wear_items_new.end());
// [/RLVa:KB]
	// Reduce wearables to max of one per type.
	removeDuplicateItems(wear_items);
// [SL:KB] - Patch: Appearance-WearableDuplicateAssets | Checked: 2011-07-24 (Catznip-2.6.0e) | Added: Catznip-2.6.0e
	removeDuplicateWearableItemsByAssetID(wear_items);
// [/SL:KB]
	filterWearableItems(wear_items, LLAgentWearables::MAX_CLOTHING_PER_TYPE);

	//
	// - Attachments: include COF contents only if appending.
	//
	LLInventoryModel::item_array_t obj_items;
	if (append)
		getDescendentsOfAssetType(cof, obj_items, LLAssetType::AT_OBJECT, false);
// [RLVa:KB] - Checked: 2010-03-05 (RLVa-1.2.0z) | Modified: RLVa-1.2.0b
	else if ( (rlv_handler_t::isEnabled()) && (gRlvAttachmentLocks.hasLockedAttachmentPoint(RLV_LOCK_ANY)) )
	{
		// Make sure that all currently locked attachments remain in COF when replacing
		getDescendentsOfAssetType(cof, obj_items, LLAssetType::AT_OBJECT, false);
		obj_items.erase(std::remove_if(obj_items.begin(), obj_items.end(), rlvPredCanRemoveItem), obj_items.end());
	}
// [/RLVa:KB]
//	getDescendentsOfAssetType(category, obj_items, LLAssetType::AT_OBJECT, false);
// [RLVa:KB] - Checked: 2010-03-05 (RLVa-1.2.0z) | Modified: RLVa-1.2.0b
	// Filter out any new attachments that can't be worn before adding them
	if ( (rlv_handler_t::isEnabled()) && (gRlvAttachmentLocks.hasLockedAttachmentPoint(RLV_LOCK_ANY)) )
		obj_items_new.erase(std::remove_if(obj_items_new.begin(), obj_items_new.end(), RlvPredCanNotWearItem(RLV_WEAR)), obj_items_new.end());
	obj_items.insert(obj_items.end(), obj_items_new.begin(), obj_items_new.end());
// [/RLVa:KB]
	removeDuplicateItems(obj_items);

	//
	// - Gestures: include COF contents only if appending.
	//
	LLInventoryModel::item_array_t gest_items;
	if (append)
		getDescendentsOfAssetType(cof, gest_items, LLAssetType::AT_GESTURE, false);
//	getDescendentsOfAssetType(category, gest_items, LLAssetType::AT_GESTURE, false);
// [RLVa:KB] - Checked: 2010-03-05 (RLVa-1.2.0z) | Added: RLVa-1.2.0b
	gest_items.insert(gest_items.end(), gest_items_new.begin(), gest_items_new.end());
// [/RLVa:KB]
	removeDuplicateItems(gest_items);
	
	// Create links to new COF contents.
	LL_DEBUGS("Avatar") << self_av_string() << "creating LLUpdateAppearanceOnDestroy" << LL_ENDL;
	LLPointer<LLInventoryCallback> link_waiter = new LLUpdateAppearanceOnDestroy(!append);

// [SL:KB] - Checked: 2010-04-24 (RLVa-1.2.0f) | Added: RLVa-1.2.0f
	if (!append)
	{
// [/SL:KB]
		// Remove current COF contents.
		bool keep_outfit_links = append;
		purgeCategory(cof, keep_outfit_links);
		gInventory.notifyObservers();
#ifndef LL_RELEASE_FOR_DOWNLOAD
	LL_DEBUGS("Avatar") << self_av_string() << "Linking body items" << LL_ENDL;
#endif
	linkAll(cof, body_items, link_waiter);

#ifndef LL_RELEASE_FOR_DOWNLOAD
	LL_DEBUGS("Avatar") << self_av_string() << "Linking wear items" << LL_ENDL;
#endif
	linkAll(cof, wear_items, link_waiter);

#ifndef LL_RELEASE_FOR_DOWNLOAD
	LL_DEBUGS("Avatar") << self_av_string() << "Linking obj items" << LL_ENDL;
#endif
	linkAll(cof, obj_items, link_waiter);

#ifndef LL_RELEASE_FOR_DOWNLOAD
	LL_DEBUGS("Avatar") << self_av_string() << "Linking gesture items" << LL_ENDL;
#endif
		linkAll(cof, gest_items, link_waiter);
// [SL:KB] - Checked: 2010-04-24 (RLVa-1.2.0f) | Added: RLVa-1.2.0f
	}
	else
	{
		// Synchronize COF
		//  -> it's possible that we don't link to any new items in which case 'link_waiter' fires when it goes out of scope below
		syncCOF(body_items, LLAssetType::AT_BODYPART, link_waiter);
		syncCOF(wear_items, LLAssetType::AT_CLOTHING, link_waiter);
		syncCOF(obj_items, LLAssetType::AT_OBJECT, link_waiter);
		syncCOF(gest_items, LLAssetType::AT_GESTURE, link_waiter);
		gInventory.notifyObservers();
	}
// [/SL:KB]

	// Add link to outfit if category is an outfit. 
// [RLVa:KB] - Checked: 2010-03-05 (RLVa-1.2.0z) | Added: RLVa-1.2.0b
	if ( (!append) && (idOutfit.notNull()) )
	{
		createBaseOutfitLink(idOutfit, link_waiter);
	}
// [/RLVa:KB]
//	if (!append)
//	{
//		createBaseOutfitLink(category, link_waiter);
//	}

	LL_DEBUGS("Avatar") << self_av_string() << "waiting for LLUpdateAppearanceOnDestroy" << LL_ENDL;
}

void LLAppearanceMgr::updatePanelOutfitName(const std::string& name)
{
	// MULTI-WEARABLE TODO
	/*LLSidepanelAppearance* panel_appearance =
		dynamic_cast<LLSidepanelAppearance *>(LLFloaterSidePanelContainer::getPanel("appearance"));
	if (panel_appearance)
	{
		panel_appearance->refreshCurrentOutfitName(name);
	}*/
}

void LLAppearanceMgr::createBaseOutfitLink(const LLUUID& category, LLPointer<LLInventoryCallback> link_waiter)
{
	const LLUUID cof = getCOF();
	LLViewerInventoryCategory* catp = gInventory.getCategory(category);
	std::string new_outfit_name = "";

	purgeBaseOutfitLink(cof);

	if (catp && catp->getPreferredType() == LLFolderType::FT_OUTFIT)
	{
		link_inventory_item(gAgent.getID(), category, cof, catp->getName(), "",
							LLAssetType::AT_LINK_FOLDER, link_waiter);
		new_outfit_name = catp->getName();
	}
	
	updatePanelOutfitName(new_outfit_name);
}

void LLAppearanceMgr::updateAgentWearables(LLWearableHoldingPattern* holder, bool append)
{
	lldebugs << "updateAgentWearables()" << llendl;
	LLInventoryItem::item_array_t items;
	LLDynamicArray< LLWearable* > wearables;
// [RLVa:KB] - Checked: 2011-03-31 (RLVa-1.3.0f) | Added: RLVa-1.3.0f
	uuid_vec_t idsCurrent; LLInventoryModel::item_array_t itemsNew;
	if (rlv_handler_t::isEnabled())
	{
		// Collect the item UUIDs of all currently worn wearables
		gAgentWearables.getWearableItemIDs(idsCurrent);
	}
// [/RLVa:KB]

	// For each wearable type, find the wearables of that type.
	for( S32 i = 0; i < LLWearableType::WT_COUNT; i++ )
	{
		for (LLWearableHoldingPattern::found_list_t::iterator iter = holder->getFoundList().begin();
			 iter != holder->getFoundList().end(); ++iter)
		{
			LLFoundData& data = *iter;
			LLWearable* wearable = data.mWearable;
			if( wearable && ((S32)wearable->getType() == i) )
			{
				LLViewerInventoryItem* item = (LLViewerInventoryItem*)gInventory.getItem(data.mItemID);
				if( item && (item->getAssetUUID() == wearable->getAssetID()) )
				{
// [RLVa:KB] - Checked: 2010-03-19 (RLVa-1.2.0g) | Modified: RLVa-1.2.0g
					// TODO-RLVa: [RLVa-1.2.1] This is fall-back code so if we don't ever trigger this code it can just be removed
					//   -> one way to trigger the assertion:
					//			1) "Replace Outfit" on a folder with clothing and an attachment that goes @addoutfit=n
					//			2) updateCOF will add/link the items into COF => no @addoutfit=n present yet => allowed
					//			3) llOwnerSay("@addoutfit=n") executes
					//			4) code below runs => @addoutfit=n conflicts with adding new wearables
					//     => if it's left as-is then the wearables won't get worn (but remain in COF which causes issues of its own)
					//     => if it's changed to debug-only then we make tge assumption that anything that makes it into COF is always OK
#ifdef RLV_DEBUG
					// NOTE: make sure we don't accidentally block setting the initial wearables
					if ( (rlv_handler_t::isEnabled()) && (RLV_WEAR_LOCKED == gRlvWearableLocks.canWear(wearable->getType())) &&
						 (!gAgentWearables.getWearableFromItemID(item->getUUID())) && (gAgentWearables.areWearablesLoaded()) )
					{
						RLV_VERIFY(RLV_WEAR_LOCKED == gRlvWearableLocks.canWear(wearable->getType()));
						continue;
					}
#endif // RLV_DEBUG
// [/RLVa:KB]
					items.put(item);
					wearables.put(wearable);
// [RLVa:KB] - Checked: 2011-03-31 (RLVa-1.3.0f) | Added: RLVa-1.3.0f
					if ( (rlv_handler_t::isEnabled()) && (gAgentWearables.areInitalWearablesLoaded()) )
					{
						// Remove the wearable from current item UUIDs if currently worn and requested, otherwise mark it as a new item
						uuid_vec_t::iterator itItemID = std::find(idsCurrent.begin(), idsCurrent.end(), item->getUUID());
						if (idsCurrent.end() != itItemID)
							idsCurrent.erase(itItemID);
						else
							itemsNew.push_back(item);
					}
// [/RLVa:KB]
				}
			}
		}
	}

// [RLVa:KB] - Checked: 2011-03-31 (RLVa-1.3.0f) | Added: RLVa-1.3.0f
	if ( (rlv_handler_t::isEnabled()) && (gAgentWearables.areInitalWearablesLoaded()) )
	{
		// We need to report removals before additions or scripts will get confused
		for (uuid_vec_t::const_iterator itItemID = idsCurrent.begin(); itItemID != idsCurrent.end(); ++itItemID)
		{
			const LLWearable* pWearable = gAgentWearables.getWearableFromItemID(*itItemID);
			if (pWearable)
				RlvBehaviourNotifyHandler::onTakeOff(pWearable->getType(), true);
		}
		for (S32 idxItem = 0, cntItem = itemsNew.count(); idxItem < cntItem; idxItem++)
		{
			RlvBehaviourNotifyHandler::onWear(itemsNew.get(idxItem)->getWearableType(), true);
		}
	}
// [/RLVa:KB]

	if(wearables.count() > 0)
	{
		gAgentWearables.setWearableOutfit(items, wearables, !append);
	}

//	dec_busy_count();
}

static void remove_non_link_items(LLInventoryModel::item_array_t &items)
{
	LLInventoryModel::item_array_t pruned_items;
	for (LLInventoryModel::item_array_t::const_iterator iter = items.begin();
		 iter != items.end();
		 ++iter)
	{
 		const LLViewerInventoryItem *item = (*iter);
		if (item && item->getIsLinkType())
		{
			pruned_items.push_back((*iter));
		}
	}
	items = pruned_items;
}

//a predicate for sorting inventory items by actual descriptions
bool sort_by_description(const LLInventoryItem* item1, const LLInventoryItem* item2)
{
	if (!item1 || !item2) 
	{
		llwarning("either item1 or item2 is NULL", 0);
		return true;
	}

	return item1->LLInventoryItem::getDescription() < item2->LLInventoryItem::getDescription();
}

void item_array_diff(LLInventoryModel::item_array_t& full_list,
					 LLInventoryModel::item_array_t& keep_list,
					 LLInventoryModel::item_array_t& kill_list)
	
{
	for (LLInventoryModel::item_array_t::iterator it = full_list.begin();
		 it != full_list.end();
		 ++it)
	{
		LLViewerInventoryItem *item = *it;
		if (keep_list.find(item) < 0) // Why on earth does LLDynamicArray need to redefine find()?
		{
			kill_list.push_back(item);
		}
	}
}

S32 LLAppearanceMgr::findExcessOrDuplicateItems(const LLUUID& cat_id,
												 LLAssetType::EType type,
												 S32 max_items,
												 LLInventoryModel::item_array_t& items_to_kill)
{
	S32 to_kill_count = 0;

	LLInventoryModel::item_array_t items;
	getDescendentsOfAssetType(cat_id, items, type, false);
	LLInventoryModel::item_array_t curr_items = items;
	removeDuplicateItems(items);
	if (max_items > 0)
	{
		filterWearableItems(items, max_items);
	}
	LLInventoryModel::item_array_t kill_items;
	item_array_diff(curr_items,items,kill_items);
	for (LLInventoryModel::item_array_t::iterator it = kill_items.begin();
		 it != kill_items.end();
		 ++it)
	{
		items_to_kill.push_back(*it);
		to_kill_count++;
	}
	return to_kill_count;
}
	
												 
void LLAppearanceMgr::enforceItemRestrictions()
{
	S32 purge_count = 0;
	LLInventoryModel::item_array_t items_to_kill;

	purge_count += findExcessOrDuplicateItems(getCOF(),LLAssetType::AT_BODYPART,
											  1, items_to_kill);
	purge_count += findExcessOrDuplicateItems(getCOF(),LLAssetType::AT_CLOTHING,
											  LLAgentWearables::MAX_CLOTHING_PER_TYPE, items_to_kill);
	purge_count += findExcessOrDuplicateItems(getCOF(),LLAssetType::AT_OBJECT,
											  -1, items_to_kill);

	if (items_to_kill.size()>0)
	{
		for (LLInventoryModel::item_array_t::iterator it = items_to_kill.begin();
			 it != items_to_kill.end();
			 ++it)
		{
			LLViewerInventoryItem *item = *it;
			LL_DEBUGS("Avatar") << self_av_string() << "purging duplicate or excess item " << item->getName() << LL_ENDL;
			gInventory.purgeObject(item->getUUID());
		}
		gInventory.notifyObservers();
	}
}

void LLAppearanceMgr::updateAppearanceFromCOF(bool update_base_outfit_ordering)
{
	if (mIsInUpdateAppearanceFromCOF)
	{
		llwarns << "Called updateAppearanceFromCOF inside updateAppearanceFromCOF, skipping" << llendl;
		return;
	}

	BoolSetter setIsInUpdateAppearanceFromCOF(mIsInUpdateAppearanceFromCOF);

	LL_INFOS("Avatar") << self_av_string() << "starting" << LL_ENDL;

	//checking integrity of the COF in terms of ordering of wearables, 
	//checking and updating links' descriptions of wearables in the COF (before analyzed for "dirty" state)
	updateClothingOrderingInfo(LLUUID::null, update_base_outfit_ordering);

	// Remove duplicate or excess wearables. Should normally be enforced at the UI level, but
	// this should catch anything that gets through.
	enforceItemRestrictions();
	
	// update dirty flag to see if the state of the COF matches
	// the saved outfit stored as a folder link
	updateIsDirty();

	//dumpCat(getCOF(),"COF, start");

	bool follow_folder_links = true;
	LLUUID current_outfit_id = getCOF();

	// Find all the wearables that are in the COF's subtree.
	lldebugs << "LLAppearanceMgr::updateFromCOF()" << llendl;
	LLInventoryModel::item_array_t wear_items;
	LLInventoryModel::item_array_t obj_items;
	LLInventoryModel::item_array_t gest_items;
	getUserDescendents(current_outfit_id, wear_items, obj_items, gest_items, follow_folder_links);
	// Get rid of non-links in case somehow the COF was corrupted.
	remove_non_link_items(wear_items);
	remove_non_link_items(obj_items);
	remove_non_link_items(gest_items);
// [SL:KB] - Patch: Apperance-Misc | Checked: 2010-11-24 (Catznip-3.0.0a) | Added: Catzip-2.4.0f
	// Since we're following folder links we might have picked up new duplicates, or exceeded MAX_CLOTHING_PER_TYPE
	removeDuplicateItems(wear_items);
	removeDuplicateItems(obj_items);
	removeDuplicateItems(gest_items);
	filterWearableItems(wear_items, LLAgentWearables::MAX_CLOTHING_PER_TYPE);
// [/SL:KB]
// [SL:KB] - Patch: Appearance-WearableDuplicateAssets | Checked: 2011-07-24 (Catznip-2.6.0e) | Added: Catznip-2.6.0e
	// Wearing two wearables that share the same asset causes some issues
	removeDuplicateWearableItemsByAssetID(wear_items);
// [/SL:KB]

	dumpItemArray(wear_items,"asset_dump: wear_item");
	dumpItemArray(obj_items,"asset_dump: obj_item");

// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-09-22 (Catznip-3.0.0a) | Added: Catznip-2.2.0a
	// Update attachments to match those requested.
	if (isAgentAvatarValid())
	{
		// Include attachments which should be in COF but don't have their link created yet
		uuid_vec_t::iterator itPendingAttachLink = mPendingAttachLinks.begin();
		while (itPendingAttachLink != mPendingAttachLinks.end())
		{
			const LLUUID& idItem = *itPendingAttachLink;
			if ( (!gAgentAvatarp->isWearingAttachment(idItem)) || (isLinkInCOF(idItem)) )
			{
				itPendingAttachLink = mPendingAttachLinks.erase(itPendingAttachLink);
				continue;
			}

			LLViewerInventoryItem* pItem = gInventory.getItem(idItem);
			if (pItem)
				obj_items.push_back(pItem);

			++itPendingAttachLink;
		}

		// Don't remove attachments until avatar is fully loaded (should reduce random attaching/detaching/reattaching at log-on)
		llinfos << "Updating " << obj_items.count() << " attachments" << llendl;
		LLAgentWearables::userUpdateAttachments(obj_items, !gAgentAvatarp->isFullyLoaded());
	}
// [/SL:KB]

	if(!wear_items.count())
	{
		LLNotificationsUtil::add("CouldNotPutOnOutfit");
		return;
	}

	//preparing the list of wearables in the correct order for LLAgentWearables
	sortItemsByActualDescription(wear_items);


	LLWearableHoldingPattern* holder = new LLWearableHoldingPattern;

//	holder->setObjItems(obj_items);
	holder->setGestItems(gest_items);
		
	// Note: can't do normal iteration, because if all the
	// wearables can be resolved immediately, then the
	// callback will be called (and this object deleted)
	// before the final getNextData().

	for(S32 i = 0; i  < wear_items.count(); ++i)
	{
		LLViewerInventoryItem *item = wear_items.get(i);
		LLViewerInventoryItem *linked_item = item ? item->getLinkedItem() : NULL;

		// Fault injection: use debug setting to test asset 
		// fetch failures (should be replaced by new defaults in
		// lost&found).
		U32 skip_type = gSavedSettings.getU32("ForceAssetFail");
// [RLVa:KB] - Checked: 2010-12-11 (RLVa-1.2.2c) | Added: RLVa-1.2.2c
		U32 missing_type = gSavedSettings.getU32("ForceMissingType");
// [/RLVa:KB]

		if (item && item->getIsLinkType() && linked_item)
		{
			LLFoundData found(linked_item->getUUID(),
							  linked_item->getAssetUUID(),
							  linked_item->getName(),
							  linked_item->getType(),
							  linked_item->isWearableType() ? linked_item->getWearableType() : LLWearableType::WT_INVALID
				);

// [RLVa:KB] - Checked: 2010-12-15 (RLVa-1.2.2c) | Modified: RLVa-1.2.2c
#ifdef LL_RELEASE_FOR_DOWNLOAD
			// Don't allow forcing an invalid wearable if the initial wearables aren't set yet, or if any wearable type is currently locked
			if ( (!rlv_handler_t::isEnabled()) || 
				 ((gAgentWearables.areInitalWearablesLoaded()) && (!gRlvWearableLocks.hasLockedWearableType(RLV_LOCK_REMOVE))) )
#endif // LL_RELEASE_FOR_DOWNLOAD
			{
				if (missing_type != LLWearableType::WT_INVALID && missing_type == found.mWearableType)
				{
					continue;
				}
// [/RLVa:KB]
			if (skip_type != LLWearableType::WT_INVALID && skip_type == found.mWearableType)
			{
				found.mAssetID.generate(); // Replace with new UUID, guaranteed not to exist in DB
			}
// [RLVa:KB] - Checked: 2010-12-15 (RLVa-1.2.2c) | Modified: RLVa-1.2.2c
			}
// [/RLVa:KB]
			//pushing back, not front, to preserve order of wearables for LLAgentWearables
			holder->getFoundList().push_back(found);
		}
		else
		{
			if (!item)
			{
				llwarns << "Attempt to wear a null item " << llendl;
			}
			else if (!linked_item)
			{
				llwarns << "Attempt to wear a broken link [ name:" << item->getName() << " ] " << llendl;
			}
		}
	}

	for (LLWearableHoldingPattern::found_list_t::iterator it = holder->getFoundList().begin();
		 it != holder->getFoundList().end(); ++it)
	{
		LLFoundData& found = *it;

		lldebugs << self_av_string() << "waiting for onWearableAssetFetch callback, asset " << found.mAssetID.asString() << llendl;

		// Fetch the wearables about to be worn.
		LLWearableList::instance().getAsset(found.mAssetID,
											found.mName,
											found.mAssetType,
											onWearableAssetFetch,
											(void*)holder);

	}

	holder->resetTime(gSavedSettings.getF32("MaxWearableWaitTime"));
	if (!holder->pollFetchCompletion())
	{
		doOnIdleRepeating(boost::bind(&LLWearableHoldingPattern::pollFetchCompletion,holder));
	}
}

// [SL:KB] - Patch: Appearance-MixedViewers | Checked: 2010-04-02 (Catznip-3.0.0a) | Added: Catznip-2.0.0a
void LLAppearanceMgr::updateAppearanceFromInitialWearables(LLInventoryModel::item_array_t& initial_items)
{
	const LLUUID& idCOF = getCOF();

	// Remove current COF contents
	purgeCategory(idCOF, false);
	gInventory.notifyObservers();

	// Create links to new COF contents
	LLPointer<LLInventoryCallback> link_waiter = new LLUpdateAppearanceOnDestroy();
	linkAll(idCOF, initial_items, link_waiter);
}
// [/SL:KB]

void LLAppearanceMgr::getDescendentsOfAssetType(const LLUUID& category,
													LLInventoryModel::item_array_t& items,
													LLAssetType::EType type,
													bool follow_folder_links)
{
	LLInventoryModel::cat_array_t cats;
	LLIsType is_of_type(type);
	gInventory.collectDescendentsIf(category,
									cats,
									items,
									LLInventoryModel::EXCLUDE_TRASH,
									is_of_type,
									follow_folder_links);
}

void LLAppearanceMgr::getUserDescendents(const LLUUID& category, 
											 LLInventoryModel::item_array_t& wear_items,
											 LLInventoryModel::item_array_t& obj_items,
											 LLInventoryModel::item_array_t& gest_items,
											 bool follow_folder_links)
{
	LLInventoryModel::cat_array_t wear_cats;
	LLFindWearables is_wearable;
	gInventory.collectDescendentsIf(category,
									wear_cats,
									wear_items,
									LLInventoryModel::EXCLUDE_TRASH,
									is_wearable,
									follow_folder_links);

	LLInventoryModel::cat_array_t obj_cats;
	LLIsType is_object( LLAssetType::AT_OBJECT );
	gInventory.collectDescendentsIf(category,
									obj_cats,
									obj_items,
									LLInventoryModel::EXCLUDE_TRASH,
									is_object,
									follow_folder_links);

	// Find all gestures in this folder
	LLInventoryModel::cat_array_t gest_cats;
	LLIsType is_gesture( LLAssetType::AT_GESTURE );
	gInventory.collectDescendentsIf(category,
									gest_cats,
									gest_items,
									LLInventoryModel::EXCLUDE_TRASH,
									is_gesture,
									follow_folder_links);
}

void LLAppearanceMgr::wearInventoryCategory(LLInventoryCategory* category, bool copy, bool append)
{
	if(!category) return;

	gAgentWearables.notifyLoadingStarted();

	LL_INFOS("Avatar") << self_av_string() << "wearInventoryCategory( " << category->getName()
			 << " )" << LL_ENDL;

	callAfterCategoryFetch(category->getUUID(),boost::bind(&LLAppearanceMgr::wearCategoryFinal,
														   &LLAppearanceMgr::instance(),
														   category->getUUID(), copy, append));
}

void LLAppearanceMgr::wearCategoryFinal(LLUUID& cat_id, bool copy_items, bool append)
{
	LL_INFOS("Avatar") << self_av_string() << "starting" << LL_ENDL;
	
	// We now have an outfit ready to be copied to agent inventory. Do
	// it, and wear that outfit normally.
	LLInventoryCategory* cat = gInventory.getCategory(cat_id);
	if(copy_items)
	{
		LLInventoryModel::cat_array_t* cats;
		LLInventoryModel::item_array_t* items;
		gInventory.getDirectDescendentsOf(cat_id, cats, items);
		std::string name;
		if(!cat)
		{
			// should never happen.
			name = "New Outfit";
		}
		else
		{
			name = cat->getName();
		}
		LLViewerInventoryItem* item = NULL;
		LLInventoryModel::item_array_t::const_iterator it = items->begin();
		LLInventoryModel::item_array_t::const_iterator end = items->end();
		LLUUID pid;
		for(; it < end; ++it)
		{
			item = *it;
			if(item)
			{
				if(LLInventoryType::IT_GESTURE == item->getInventoryType())
				{
					pid = gInventory.findCategoryUUIDForType(LLFolderType::FT_GESTURE);
				}
				else
				{
					pid = gInventory.findCategoryUUIDForType(LLFolderType::FT_CLOTHING);
				}
				break;
			}
		}
		if(pid.isNull())
		{
			pid = gInventory.getRootFolderID();
		}
		
		LLUUID new_cat_id = gInventory.createNewCategory(
			pid,
			LLFolderType::FT_NONE,
			name);
		LLPointer<LLInventoryCallback> cb = new LLWearInventoryCategoryCallback(new_cat_id, append);
		it = items->begin();
		for(; it < end; ++it)
		{
			item = *it;
			if(item)
			{
				copy_inventory_item(
					gAgent.getID(),
					item->getPermissions().getOwner(),
					item->getUUID(),
					new_cat_id,
					std::string(),
					cb);
			}
		}
		// BAP fixes a lag in display of created dir.
		gInventory.notifyObservers();
	}
	else
	{
		// Wear the inventory category.
		LLAppearanceMgr::instance().wearInventoryCategoryOnAvatar(cat, append);
	}
}

// *NOTE: hack to get from avatar inventory to avatar
void LLAppearanceMgr::wearInventoryCategoryOnAvatar( LLInventoryCategory* category, bool append )
{
	// Avoid unintentionally overwriting old wearables.  We have to do
	// this up front to avoid having to deal with the case of multiple
	// wearables being dirty.
	if(!category) return;

	LL_INFOS("Avatar") << self_av_string() << "wearInventoryCategoryOnAvatar '" << category->getName()
			 << "'" << LL_ENDL;
			 	
	if (gAgentCamera.cameraCustomizeAvatar())
	{
		// switching to outfit editor should automagically save any currently edited wearable
		//LLFloaterSidePanelContainer::showPanel("appearance", LLSD().with("type", "edit_outfit"));	// MULTI-WEARABLES TODO
	}

	LLAppearanceMgr::changeOutfit(TRUE, category->getUUID(), append);
}

void LLAppearanceMgr::wearOutfitByName(const std::string& name)
{
	LL_INFOS("Avatar") << self_av_string() << "Wearing category " << name << LL_ENDL;
	//inc_busy_count();

	LLInventoryModel::cat_array_t cat_array;
	LLInventoryModel::item_array_t item_array;
	LLNameCategoryCollector has_name(name);
	gInventory.collectDescendentsIf(gInventory.getRootFolderID(),
									cat_array,
									item_array,
									LLInventoryModel::EXCLUDE_TRASH,
									has_name);
	bool copy_items = false;
	LLInventoryCategory* cat = NULL;
	if (cat_array.count() > 0)
	{
		// Just wear the first one that matches
		cat = cat_array.get(0);
	}
	else
	{
		gInventory.collectDescendentsIf(LLUUID::null,
										cat_array,
										item_array,
										LLInventoryModel::EXCLUDE_TRASH,
										has_name);
		if(cat_array.count() > 0)
		{
			cat = cat_array.get(0);
			copy_items = true;
		}
	}

	if(cat)
	{
		LLAppearanceMgr::wearInventoryCategory(cat, copy_items, false);
	}
	else
	{
		llwarns << "Couldn't find outfit " <<name<< " in wearOutfitByName()"
				<< llendl;
	}

	//dec_busy_count();
}

bool areMatchingWearables(const LLViewerInventoryItem *a, const LLViewerInventoryItem *b)
{
	return (a->isWearableType() && b->isWearableType() &&
			(a->getWearableType() == b->getWearableType()));
}

class LLDeferredCOFLinkObserver: public LLInventoryObserver
{
public:
	LLDeferredCOFLinkObserver(const LLUUID& item_id, bool do_update, LLPointer<LLInventoryCallback> cb = NULL):
		mItemID(item_id),
		mDoUpdate(do_update),
		mCallback(cb)
	{
	}

	~LLDeferredCOFLinkObserver()
	{
	}
	
	/* virtual */ void changed(U32 mask)
	{
		const LLInventoryItem *item = gInventory.getItem(mItemID);
		if (item)
		{
			gInventory.removeObserver(this);
			LLAppearanceMgr::instance().addCOFItemLink(item,mDoUpdate,mCallback);
			delete this;
		}
	}

private:
	const LLUUID mItemID;
	bool mDoUpdate;
	LLPointer<LLInventoryCallback> mCallback;
};


// BAP - note that this runs asynchronously if the item is not already loaded from inventory.
// Dangerous if caller assumes link will exist after calling the function.
void LLAppearanceMgr::addCOFItemLink(const LLUUID &item_id, bool do_update, LLPointer<LLInventoryCallback> cb)
{
	const LLInventoryItem *item = gInventory.getItem(item_id);
	if (!item)
	{
		LLDeferredCOFLinkObserver *observer = new LLDeferredCOFLinkObserver(item_id, do_update, cb);
		gInventory.addObserver(observer);
	}
	else
	{
		addCOFItemLink(item, do_update, cb);
	}
}

void LLAppearanceMgr::addCOFItemLink(const LLInventoryItem *item, bool do_update, LLPointer<LLInventoryCallback> cb)
{		
	const LLViewerInventoryItem *vitem = dynamic_cast<const LLViewerInventoryItem*>(item);
	if (!vitem)
	{
		llwarns << "not an llviewerinventoryitem, failed" << llendl;
		return;
	}

	gInventory.addChangedMask(LLInventoryObserver::LABEL, vitem->getLinkedUUID());

	LLInventoryModel::cat_array_t cat_array;
	LLInventoryModel::item_array_t item_array;
	gInventory.collectDescendents(LLAppearanceMgr::getCOF(),
								  cat_array,
								  item_array,
								  LLInventoryModel::EXCLUDE_TRASH);
	bool linked_already = false;
	U32 count = 0;
	for (S32 i=0; i<item_array.count(); i++)
	{
		// Are these links to the same object?
		const LLViewerInventoryItem* inv_item = item_array.get(i).get();
		const LLWearableType::EType wearable_type = inv_item->getWearableType();

		const bool is_body_part =    (wearable_type == LLWearableType::WT_SHAPE) 
								  || (wearable_type == LLWearableType::WT_HAIR) 
								  || (wearable_type == LLWearableType::WT_EYES)
								  || (wearable_type == LLWearableType::WT_SKIN);

		if (inv_item->getLinkedUUID() == vitem->getLinkedUUID())
		{
			linked_already = true;
		}
		// Are these links to different items of the same body part
		// type? If so, new item will replace old.
		else if ((vitem->isWearableType()) && (vitem->getWearableType() == wearable_type))
		{
			++count;
			if (is_body_part && inv_item->getIsLinkType()  && (vitem->getWearableType() == wearable_type))
			{
				gInventory.purgeObject(inv_item->getUUID());
			}
			else if (count >= LLAgentWearables::MAX_CLOTHING_PER_TYPE)
			{
				// MULTI-WEARABLES: make sure we don't go over MAX_CLOTHING_PER_TYPE
				gInventory.purgeObject(inv_item->getUUID());
			}
// [SL:KB] - Patch: Appearance-WearableDuplicateAssets | Checked: 2011-07-24 (Catznip-2.6.0e) | Added: Catznip-2.6.0e
			else if ( (vitem->getWearableType() == wearable_type) && (vitem->getAssetUUID() == inv_item->getAssetUUID()) )
			{
				// Only allow one wearable per unique asset
				linked_already = true;
			}
// [/SL:KB]
		}
	}

	if (linked_already)
	{
		if (do_update)
		{	
			LLAppearanceMgr::updateAppearanceFromCOF();
		}
		return;
	}
	else
	{
		if(do_update && cb.isNull())
		{
			cb = new ModifiedCOFCallback;
		}
		std::string description = vitem->getIsLinkType() ? vitem->getDescription() : "";
		if(description.empty())
		{
			LLWearable* wearable = gAgentWearables.getWearableFromItemID(vitem->getLinkedUUID());
			if(wearable)
			{
				U32 index = gAgentWearables.getWearableIndex(wearable);
				if(index < LLAgentWearables::MAX_CLOTHING_PER_TYPE)
				{
					std::ostringstream order_num;
					order_num << ORDER_NUMBER_SEPARATOR << wearable->getType() * 100 + index;
					description = order_num.str();
				}
			}
		}
		link_inventory_item( gAgent.getID(),
							 vitem->getLinkedUUID(),
							 getCOF(),
							 vitem->getName(),
							 description,
							 LLAssetType::AT_LINK,
							 cb);
	}
	return;
}

// BAP remove ensemble code for 2.1?
void LLAppearanceMgr::addEnsembleLink( LLInventoryCategory* cat, bool do_update )
{
#if SUPPORT_ENSEMBLES
	// BAP add check for already in COF.
	LLPointer<LLInventoryCallback> cb = do_update ? new ModifiedCOFCallback : 0;
	link_inventory_item( gAgent.getID(),
						 cat->getLinkedUUID(),
						 getCOF(),
						 cat->getName(),
						 cat->getDescription(),
						 LLAssetType::AT_LINK_FOLDER,
						 cb);
#endif
}

void LLAppearanceMgr::removeCOFItemLinks(const LLUUID& item_id, bool do_update)
{
	gInventory.addChangedMask(LLInventoryObserver::LABEL, item_id);

	LLInventoryModel::cat_array_t cat_array;
	LLInventoryModel::item_array_t item_array;
	gInventory.collectDescendents(LLAppearanceMgr::getCOF(),
								  cat_array,
								  item_array,
								  LLInventoryModel::EXCLUDE_TRASH);
	for (S32 i=0; i<item_array.count(); i++)
	{
		const LLInventoryItem* item = item_array.get(i).get();
		if (item->getIsLinkType() && item->getLinkedUUID() == item_id)
		{
			gInventory.purgeObject(item->getUUID());
		}
	}
	if (do_update)
	{
		LLAppearanceMgr::updateAppearanceFromCOF();
	}
}

void LLAppearanceMgr::removeCOFLinksOfType(LLWearableType::EType type, bool do_update)
{
	LLFindWearablesOfType filter_wearables_of_type(type);
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	LLInventoryModel::item_array_t::const_iterator it;

	gInventory.collectDescendentsIf(getCOF(), cats, items, true, filter_wearables_of_type);
	for (it = items.begin(); it != items.end(); ++it)
	{
		const LLViewerInventoryItem* item = *it;
		if (item->getIsLinkType()) // we must operate on links only
		{
			gInventory.purgeObject(item->getUUID());
		}
	}

	if (do_update)
	{
		updateAppearanceFromCOF();
	}
}

bool sort_by_linked_uuid(const LLViewerInventoryItem* item1, const LLViewerInventoryItem* item2)
{
	if (!item1 || !item2)
	{
		llwarning("item1, item2 cannot be null, something is very wrong", 0);
		return true;
	}

	return item1->getLinkedUUID() < item2->getLinkedUUID();
}

void LLAppearanceMgr::updateIsDirty()
{
	LLUUID cof = getCOF();
	LLUUID base_outfit;

	// find base outfit link 
	const LLViewerInventoryItem* base_outfit_item = getBaseOutfitLink();
	LLViewerInventoryCategory* catp = NULL;
	if (base_outfit_item && base_outfit_item->getIsLinkType())
	{
		catp = base_outfit_item->getLinkedCategory();
	}
	if(catp && catp->getPreferredType() == LLFolderType::FT_OUTFIT)
	{
		base_outfit = catp->getUUID();
	}

	// Set dirty to "false" if no base outfit found to disable "Save"
	// and leave only "Save As" enabled in My Outfits.
	mOutfitIsDirty = false;

	if (base_outfit.notNull())
	{
		LLIsOfAssetType collector = LLIsOfAssetType(LLAssetType::AT_LINK);

		LLInventoryModel::cat_array_t cof_cats;
		LLInventoryModel::item_array_t cof_items;
		gInventory.collectDescendentsIf(cof, cof_cats, cof_items,
									  LLInventoryModel::EXCLUDE_TRASH, collector);

		LLInventoryModel::cat_array_t outfit_cats;
		LLInventoryModel::item_array_t outfit_items;
		gInventory.collectDescendentsIf(base_outfit, outfit_cats, outfit_items,
									  LLInventoryModel::EXCLUDE_TRASH, collector);

		if(outfit_items.count() != cof_items.count())
		{
			// Current outfit folder should have one more item than the outfit folder.
			// this one item is the link back to the outfit folder itself.
			mOutfitIsDirty = true;
			return;
		}

		//"dirty" - also means a difference in linked UUIDs and/or a difference in wearables order (links' descriptions)
		std::sort(cof_items.begin(), cof_items.end(), sort_by_linked_uuid);
		std::sort(outfit_items.begin(), outfit_items.end(), sort_by_linked_uuid);

		for (U32 i = 0; i < cof_items.size(); ++i)
		{
			LLViewerInventoryItem *item1 = cof_items.get(i);
			LLViewerInventoryItem *item2 = outfit_items.get(i);

			if (item1->getLinkedUUID() != item2->getLinkedUUID() || 
				item1->getName() != item2->getName() ||
				item1->LLInventoryItem::getDescription() != item2->LLInventoryItem::getDescription())
			{
				mOutfitIsDirty = true;
				return;
			}
		}
	}
}

// *HACK: Must match name in Library or agent inventory
const std::string ROOT_GESTURES_FOLDER = "Gestures";
const std::string COMMON_GESTURES_FOLDER = "Common Gestures";
const std::string MALE_GESTURES_FOLDER = "Male Gestures";
const std::string FEMALE_GESTURES_FOLDER = "Female Gestures";
const std::string SPEECH_GESTURES_FOLDER = "Speech Gestures";
const std::string OTHER_GESTURES_FOLDER = "Other Gestures";

void LLAppearanceMgr::copyLibraryGestures()
{
	LL_INFOS("Avatar") << self_av_string() << "Copying library gestures" << LL_ENDL;

	// Copy gestures
	LLUUID lib_gesture_cat_id =
		gInventory.findCategoryUUIDForType(LLFolderType::FT_GESTURE,false,true);
	if (lib_gesture_cat_id.isNull())
	{
		llwarns << "Unable to copy gestures, source category not found" << llendl;
	}
	LLUUID dst_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_GESTURE);

	std::vector<std::string> gesture_folders_to_copy;
	gesture_folders_to_copy.push_back(MALE_GESTURES_FOLDER);
	gesture_folders_to_copy.push_back(FEMALE_GESTURES_FOLDER);
	gesture_folders_to_copy.push_back(COMMON_GESTURES_FOLDER);
	gesture_folders_to_copy.push_back(SPEECH_GESTURES_FOLDER);
	gesture_folders_to_copy.push_back(OTHER_GESTURES_FOLDER);

	for(std::vector<std::string>::iterator it = gesture_folders_to_copy.begin();
		it != gesture_folders_to_copy.end();
		++it)
	{
		std::string& folder_name = *it;

		LLPointer<LLInventoryCallback> cb(NULL);

		// After copying gestures, activate Common, Other, plus
		// Male and/or Female, depending upon the initial outfit gender.
		ESex gender = gAgentAvatarp->getSex();

		std::string activate_male_gestures;
		std::string activate_female_gestures;
		switch (gender) {
			case SEX_MALE:
				activate_male_gestures = MALE_GESTURES_FOLDER;
				break;
			case SEX_FEMALE:
				activate_female_gestures = FEMALE_GESTURES_FOLDER;
				break;
			case SEX_BOTH:
				activate_male_gestures = MALE_GESTURES_FOLDER;
				activate_female_gestures = FEMALE_GESTURES_FOLDER;
				break;
		}

		if (folder_name == activate_male_gestures ||
			folder_name == activate_female_gestures ||
			folder_name == COMMON_GESTURES_FOLDER ||
			folder_name == OTHER_GESTURES_FOLDER)
		{
			cb = new ActivateGestureCallback;
		}

		LLUUID cat_id = findDescendentCategoryIDByName(lib_gesture_cat_id,folder_name);
		if (cat_id.isNull())
		{
			llwarns << self_av_string() << "failed to find gesture folder for " << folder_name << llendl;
		}
		else
		{
			LL_DEBUGS("Avatar") << self_av_string() << "initiating fetch and copy for " << folder_name << " cat_id " << cat_id << LL_ENDL;
			callAfterCategoryFetch(cat_id,
								   boost::bind(&LLAppearanceMgr::shallowCopyCategory,
											   &LLAppearanceMgr::instance(),
											   cat_id, dst_id, cb));
		}
	}
}

void LLAppearanceMgr::autopopulateOutfits()
{
	// If this is the very first time the user has logged into viewer2+ (from a legacy viewer, or new account)
	// then auto-populate outfits from the library into the My Outfits folder.

	LL_INFOS("Avatar") << self_av_string() << "avatar fully visible" << LL_ENDL;

	static bool check_populate_my_outfits = true;
	if (check_populate_my_outfits && 
		(LLInventoryModel::getIsFirstTimeInViewer2() 
		 || gSavedSettings.getBOOL("MyOutfitsAutofill")))
	{
		gAgentWearables.populateMyOutfitsFolder();
	}
	check_populate_my_outfits = false;
}

// Handler for anything that's deferred until avatar de-clouds.
void LLAppearanceMgr::onFirstFullyVisible()
{
	gAgentAvatarp->outputRezTiming("Avatar fully loaded");
	gAgentAvatarp->reportAvatarRezTime();
	gAgentAvatarp->debugAvatarVisible();

	// The auto-populate is failing at the point of generating outfits
	// folders, so don't do the library copy until that is resolved.
	// autopopulateOutfits();

	// If this is the first time we've ever logged in,
	// then copy default gestures from the library.
	if (gAgent.isFirstLogin()) {
		copyLibraryGestures();
	}
}

bool LLAppearanceMgr::updateBaseOutfit()
{
	if (isOutfitLocked())
	{
		// don't allow modify locked outfit
		llassert(!isOutfitLocked());
		return false;
	}
	setOutfitLocked(true);

	gAgentWearables.notifyLoadingStarted();

	const LLUUID base_outfit_id = getBaseOutfitUUID();
	if (base_outfit_id.isNull()) return false;

	updateClothingOrderingInfo();

	// in a Base Outfit we do not remove items, only links
	purgeCategory(base_outfit_id, false);


	LLPointer<LLInventoryCallback> dirty_state_updater = new LLUpdateDirtyState();

	//COF contains only links so we copy to the Base Outfit only links
	shallowCopyCategoryContents(getCOF(), base_outfit_id, dirty_state_updater);

	return true;
}

void LLAppearanceMgr::divvyWearablesByType(const LLInventoryModel::item_array_t& items, wearables_by_type_t& items_by_type)
{
	items_by_type.resize(LLWearableType::WT_COUNT);
	if (items.empty()) return;

	for (S32 i=0; i<items.count(); i++)
	{
		LLViewerInventoryItem *item = items.get(i);
		if (!item)
		{
			LL_WARNS("Appearance") << "NULL item found" << llendl;
			continue;
		}
		// Ignore non-wearables.
		if (!item->isWearableType())
			continue;
		LLWearableType::EType type = item->getWearableType();
		if(type < 0 || type >= LLWearableType::WT_COUNT)
		{
			LL_WARNS("Appearance") << "Invalid wearable type. Inventory type does not match wearable flag bitfield." << LL_ENDL;
			continue;
		}
		items_by_type[type].push_back(item);
	}
}

std::string build_order_string(LLWearableType::EType type, U32 i)
{
		std::ostringstream order_num;
		order_num << ORDER_NUMBER_SEPARATOR << type * 100 + i;
		return order_num.str();
}

struct WearablesOrderComparator
{
	LOG_CLASS(WearablesOrderComparator);
	WearablesOrderComparator(const LLWearableType::EType type)
	{
		mControlSize = build_order_string(type, 0).size();
	};

	bool operator()(const LLInventoryItem* item1, const LLInventoryItem* item2)
	{
		if (!item1 || !item2)
		{
			llwarning("either item1 or item2 is NULL", 0);
			return true;
		}
		
		const std::string& desc1 = item1->LLInventoryItem::getDescription();
		const std::string& desc2 = item2->LLInventoryItem::getDescription();
		
		bool item1_valid = (desc1.size() == mControlSize) && (ORDER_NUMBER_SEPARATOR == desc1[0]);
		bool item2_valid = (desc2.size() == mControlSize) && (ORDER_NUMBER_SEPARATOR == desc2[0]);

		if (item1_valid && item2_valid)
			return desc1 < desc2;

		//we need to sink down invalid items: items with empty descriptions, items with "Broken link" descriptions,
		//items with ordering information but not for the associated wearables type
		if (!item1_valid && item2_valid) 
			return false;

		return true;
	}

	U32 mControlSize;
};

void LLAppearanceMgr::updateClothingOrderingInfo(LLUUID cat_id, bool update_base_outfit_ordering)
{
	if (cat_id.isNull())
	{
		cat_id = getCOF();
		if (update_base_outfit_ordering)
		{
			const LLUUID base_outfit_id = getBaseOutfitUUID();
			if (base_outfit_id.notNull())
			{
				updateClothingOrderingInfo(base_outfit_id,false);
			}
		}
	}

	// COF is processed if cat_id is not specified
	LLInventoryModel::item_array_t wear_items;
	getDescendentsOfAssetType(cat_id, wear_items, LLAssetType::AT_CLOTHING, false);

	wearables_by_type_t items_by_type(LLWearableType::WT_COUNT);
	divvyWearablesByType(wear_items, items_by_type);

	bool inventory_changed = false;
	for (U32 type = LLWearableType::WT_SHIRT; type < LLWearableType::WT_COUNT; type++)
	{
		
		U32 size = items_by_type[type].size();
		if (!size) continue;

		//sinking down invalid items which need reordering
		std::sort(items_by_type[type].begin(), items_by_type[type].end(), WearablesOrderComparator((LLWearableType::EType) type));

		//requesting updates only for those links which don't have "valid" descriptions
		for (U32 i = 0; i < size; i++)
		{
			LLViewerInventoryItem* item = items_by_type[type][i];
			if (!item) continue;

			std::string new_order_str = build_order_string((LLWearableType::EType)type, i);
			if (new_order_str == item->LLInventoryItem::getDescription()) continue;

			item->setDescription(new_order_str);
			item->setComplete(TRUE);
 			item->updateServer(FALSE);
			gInventory.updateItem(item);
			
			inventory_changed = true;
		}
	}

	//*TODO do we really need to notify observers?
	if (inventory_changed) gInventory.notifyObservers();
}



class LLScrollOnFirstItem : public LLInventoryCallback
{
public:
	LLScrollOnFirstItem(const LLUUID&folder_id, bool do_scroll) : mFirstItemCreated(!do_scroll), mFolderID(folder_id)
	{}

	virtual void fire(const LLUUID& item_id)
	{
		if(mFirstItemCreated)
			return;
		mFirstItemCreated = true;
		if (LLInventoryPanel::getActiveInventoryPanel())
		{
			if( LLFolderView* root = LLInventoryPanel::getActiveInventoryPanel()->getRootFolder())
			{
				LLFolderViewItem* folder = dynamic_cast<LLFolderViewFolder*>(root->getItemByID(mFolderID));
				if(folder)
				{
					folder->openItem();
					root->setSelection(folder,true,false);
					root->scrollToShowSelection();
				}
			}
		}
	}
	bool mFirstItemCreated;
	LLUUID mFolderID;
};

class LLShowCreatedOutfit: public LLScrollOnFirstItem
{
public:
	LLShowCreatedOutfit(const LLUUID& folder_id, bool show_panel = true): LLScrollOnFirstItem(folder_id, show_panel), mFolderID(folder_id), mShowPanel(show_panel)
	{}

	virtual ~LLShowCreatedOutfit()
	{
		if (!LLApp::isRunning())
		{
			llwarns << "called during shutdown, skipping" << llendl;
			return;
		}

		LLSD key;
		
		//EXT-7727. For new accounts LLShowCreatedOutfit is created during login process
		// add may be processed after login process is finished
		// MULTI-WEARABLES TODO
		/*if (mShowPanel)
		{
			LLFloaterSidePanelContainer::showPanel("appearance", "panel_outfits_inventory", key);

		}
		LLOutfitsList *outfits_list =
			dynamic_cast<LLOutfitsList*>(LLFloaterSidePanelContainer::getPanel("appearance", "outfitslist_tab"));
		if (outfits_list)
		{
			outfits_list->setSelectedOutfitByUUID(mFolderID);
		}*/

		LLAppearanceMgr::getInstance()->updateIsDirty();
		gAgentWearables.notifyLoadingFinished(); // New outfit is saved.
		LLAppearanceMgr::getInstance()->updatePanelOutfitName("");
	}

protected:
	LLUUID mFolderID;
	bool mShowPanel;
};

LLUUID LLAppearanceMgr::makeNewOutfitLinks(const std::string& new_folder_name, bool show_panel)
{
	if (!isAgentAvatarValid()) return LLUUID::null;

	gAgentWearables.notifyLoadingStarted();

	// First, make a folder in the My Outfits directory.
	const LLUUID parent_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_MY_OUTFITS);
	LLUUID folder_id = gInventory.createNewCategory(
		parent_id,
		LLFolderType::FT_OUTFIT,
		new_folder_name);

	updateClothingOrderingInfo();

	LLPointer<LLInventoryCallback> cb = new LLShowCreatedOutfit(folder_id,show_panel);
	shallowCopyCategoryContents(getCOF(),folder_id, cb);
	createBaseOutfitLink(folder_id, cb);

	dumpCat(folder_id,"COF, new outfit");

	return folder_id;
}

//Given an array of items from COF. v3 outfit behavior.
LLUUID LLAppearanceMgr::makeNewOutfitLinks(const std::string& new_folder_name, LLInventoryModel::item_array_t& items, bool show_panel )
{
	if (!isAgentAvatarValid()) return LLUUID::null;
	else if (items.empty()) return LLUUID::null;

	gAgentWearables.notifyLoadingStarted();

	// First, make a folder in the My Outfits directory.
	const LLUUID parent_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_MY_OUTFITS);
	LLUUID folder_id = gInventory.createNewCategory(
		parent_id,
		LLFolderType::FT_OUTFIT,
		new_folder_name);

	updateClothingOrderingInfo();

	LLPointer<LLInventoryCallback> cb = new LLShowCreatedOutfit(folder_id,show_panel);
	copyItems(folder_id, &items, cb);
	createBaseOutfitLink(folder_id, cb);

	dumpCat(folder_id,"COF, new outfit");

	return folder_id;
}

//Creates item copies before links and ties all requests to a sole handler
//Requests are batched into subbatches, as too many requests at once causes the sim to
//stall with the inventory requests.
//This handler will also ensure all 'copy' requests are finished before 'link' requests are
//sent out. This behavior isn't really needed for nomod/nocopy items, but it is for multi-worn 
//clothing.
//Note that the 'wear' process is pretty convoluted, but its a cludge to get rlva support in without
//tinkering with LLAppearanceMgr further.
//To use this:
// 1) assign an LLPointer the newly created LLCreateLegacyOutfit object.
// 2) Stuff with requests via makeLink and makeCopy
// 3) Call dispatch()
// 4) Let the LLPointer go out of scope.
class LLCreateLegacyOutfit : public LLShowCreatedOutfit
{
public:
	LLCreateLegacyOutfit(const LLUUID& folder_id, bool show_panel) : 
		LLShowCreatedOutfit(folder_id, show_panel),	mFolderID(folder_id), mFailed(false)
	{}
	virtual ~LLCreateLegacyOutfit()
	{
		if (!LLApp::isRunning() || mFailed)
			return;

		LLInventoryModel::item_array_t body_items, wear_items, obj_items, gest_items;
		for(std::set<LLUUID>::const_iterator it = mWearItems.begin(); it != mWearItems.end(); ++it)
		{
			LLViewerInventoryItem* item = gInventory.getItem(*it);
			if(item)
			{
				switch(item->getType())
				{
				case LLAssetType::AT_BODYPART:
					body_items.push_back(item);
					break;
				case LLAssetType::AT_CLOTHING:
					wear_items.push_back(item);
					break;
				case LLAssetType::AT_OBJECT:
					obj_items.push_back(item);
					break;
				case LLAssetType::AT_GESTURE:
					gest_items.push_back(item);
					break;
				default:
					break;
				}
			}
		}
		
		if(!body_items.empty() || !wear_items.empty() || !obj_items.empty() || !gest_items.empty())
			LLAppearanceMgr::instance().updateCOF(body_items, wear_items, obj_items, gest_items, false);
	}
private:
	class LLCreateBase : public LLInventoryCallback
	{
	public:
		LLCreateBase(LLViewerInventoryItem* item, const LLUUID& folder_id, LLPointer<LLCreateLegacyOutfit> cb) :
			mCallback(cb), mItem(item), mFolderID(folder_id)
		{}
		virtual ~LLCreateBase()
		{
			if(mCallback)
				mCallback->finished(this, LLUUID::null);
		}
		virtual void dispatch() = 0;
		virtual void fire(const LLUUID& item_id)
		{
			mCallback->finished(this, item_id);
			mCallback = NULL;
		}
		const LLViewerInventoryItem* getItem() const {return mItem;}
	protected:
		LLPointer<LLViewerInventoryItem> mItem;
		LLPointer<LLCreateLegacyOutfit> mCallback;
		const LLUUID mFolderID;
	};
	class LLCreateCopy : public LLCreateBase
	{
	public: 
		LLCreateCopy(LLViewerInventoryItem* item, bool create_copy, const LLUUID& folder_id, LLPointer<LLCreateLegacyOutfit> cb) :
			LLCreateBase(item,folder_id,cb), mCreateLink(create_copy),
			mLinkDesc((mCreateLink && item->getIsLinkType()) ? item->LLInventoryItem::getDescription() : "" )
		{}
		virtual void dispatch()
		{
			const LLViewerInventoryItem* base_item = mItem->getLinkedItem() ? mItem->getLinkedItem() : mItem.get();
			copy_inventory_item(gAgent.getID(),
								base_item->getPermissions().getOwner(),
								base_item->getUUID(),
								mFolderID,
								base_item->getName(),
								this);
		}
		virtual void fire(const LLUUID& item_id)
		{
			if(mCreateLink)
				mCallback->makeLink(gInventory.getItem(item_id), mLinkDesc);
			LLCreateBase::fire(item_id);
		}
	private:
		bool mCreateLink;
		std::string mLinkDesc;
	};
	class LLCreateLink : public LLCreateBase
	{
	public:
		LLCreateLink(LLViewerInventoryItem* item, const std::string& desc, const LLUUID& folder_id, LLPointer<LLCreateLegacyOutfit> cb) :
			LLCreateBase(item,folder_id,cb), mDesc(desc)
		{}
		virtual void dispatch()
		{
			link_inventory_item(gAgent.getID(),
								mItem->getLinkedUUID(),
								mFolderID,
								mItem->getName(),
								mDesc,
								LLAssetType::AT_LINK,
								this);
		}
	private:
		const std::string mDesc;
	};
public:
	void makeLink(LLViewerInventoryItem* item, const std::string desc)
	{
		if(!item)
			return;
		mPendingLinks.push_back(new LLCreateLink(item, desc, mFolderID, this));
	}
	void makeCopy(LLViewerInventoryItem* item, bool create_link)
	{	
		if(!item)
			return;
		mPendingCopies.push_back(new LLCreateCopy(item, create_link, mFolderID, this));
	}
	void finished(const LLCreateBase* cb, const LLUUID item_id)
	{
		if(!LLApp::isRunning())
		{
			mPendingCopies.clear();
			mPendingLinks.clear();
			return;
		}
		if(item_id.notNull())
			LLShowCreatedOutfit::fire(item_id);

		std::vector<const LLCreateBase*>::iterator it = std::find(mActiveRequests.begin(), mActiveRequests.end(),cb);
		if(it != mActiveRequests.end())
		{
			const LLViewerInventoryItem* old_item = (*it)->getItem();
			if(item_id.notNull())
			{
				const LLViewerInventoryItem* item = gInventory.getItem(item_id);

				if ((rlv_handler_t::isEnabled()) &&
					//If the old item can be removed, but a new one can't take its place, then just use the original item again.
					(((rlvPredCanRemoveItem(old_item) && !rlvPredCanWearItem(item,RLV_WEAR_REPLACE))) ||
					//If the old item cannot be removed then just use the original item again.
					!rlvPredCanRemoveItem(old_item)))
				{
					item = old_item;
				}
				if(item->getIsLinkType())
					mWearItems.erase(item->getLinkedUUID());
				mWearItems.insert(item->getUUID());
			}
			else 
				mWearItems.insert(old_item->getUUID());

			mActiveRequests.erase(it);

			if(!item_id.notNull())
				mFailed = true;

			if(mActiveRequests.empty())
				dispatch();	//Fire off any pending requests.
		}
	}
	void dispatch()
	{
		const S32 max_batch = 5;
		S32 count=0;
		
		if(!sendRequests(mPendingCopies,count,max_batch))
			sendRequests(mPendingLinks,count,max_batch);	//IFF there are NO copy requests pending.

		gInventory.notifyObservers();
	}
private:
	bool sendRequests(std::vector<LLPointer<LLCreateBase> >& list, S32& count, const S32& max_batch)
	{
		bool handled = false;
		for(std::vector<LLPointer<LLCreateBase> >::iterator it = list.begin();it!=list.end();)
		{
			if(count >= max_batch)
				break;
			LLPointer<LLCreateBase> cb = (*it);
			it=list.erase(it);
			if(cb)
			{
				cb->dispatch();
				mActiveRequests.push_back(cb.get());
				++count;
				handled = true;
			}
		}
		return handled;
	}

	LLUUID mFolderID;
	bool mFailed;
	std::vector<LLPointer<LLCreateBase> > mPendingCopies;
	std::vector<LLPointer<LLCreateBase> > mPendingLinks;
	std::set<LLUUID> mWearItems;
	std::vector<const LLCreateBase*> mActiveRequests;
};


//Given an array of items from COF. Will only use links for no-copy, no-mod, or multi-worn clothing.
LLUUID LLAppearanceMgr::makeNewOutfitLegacy(const std::string& new_folder_name, LLInventoryModel::item_array_t& items, bool use_links, bool show_panel )
{
	if (!isAgentAvatarValid()) return LLUUID::null;
	else if (items.empty()) return LLUUID::null;

	gAgentWearables.notifyLoadingStarted();

	// First, make a folder in the My Outfits directory.
	const LLUUID parent_id = gInventory.findCategoryUUIDForType(LLFolderType::FT_CLOTHING);
	LLUUID folder_id = gInventory.createNewCategory(
		parent_id,
		LLFolderType::FT_NONE,
		new_folder_name);

	updateClothingOrderingInfo();

	LLInventoryModel::item_array_t base_items;
	LLInventoryModel::item_array_t remove_items;

	LLPointer<LLCreateLegacyOutfit> cb = new LLCreateLegacyOutfit(folder_id,show_panel);

	for (LLInventoryModel::item_array_t::const_iterator iter = items.begin();
		 iter != items.end();
		 ++iter)
	{
		LLViewerInventoryItem* item = (*iter);
		LLViewerInventoryItem* base_item = item->getLinkedItem() ? item->getLinkedItem() : item;
		bool is_copy = base_item->getPermissions().allowCopyBy(gAgent.getID());
		//Just treat 'object' type as modifiable... permission slam screws them up pretty well.
		bool is_mod = base_item->getInventoryType() == LLInventoryType::IT_OBJECT || base_item->getPermissions().allowModifyBy(gAgent.getID());
		//If it's multi-worn we want to create a copy of the item if possible AND create a new link to that new copy with the same desc as the old link.
		bool is_multi = base_item->isWearableType() && gAgentWearables.getWearableCount(base_item->getWearableType()) > 1 ;

		if( use_links && (!is_copy || !is_mod) )
		{
			cb->makeLink(item,item->LLInventoryItem::getDescription());
		}
		else if( is_copy )
		{
			cb->makeCopy(item,is_multi && use_links);
		}
	}
	cb->dispatch();

	return folder_id;
}

void LLAppearanceMgr::wearBaseOutfit()
{
	const LLUUID& base_outfit_id = getBaseOutfitUUID();
	if (base_outfit_id.isNull()) return;
	
	updateCOF(base_outfit_id);
}

void LLAppearanceMgr::removeItemFromAvatar(const LLUUID& id_to_remove)
{
	LLViewerInventoryItem * item_to_remove = gInventory.getItem(id_to_remove);
	if (!item_to_remove) return;

	switch (item_to_remove->getType())
	{
		case LLAssetType::AT_CLOTHING:
//			if (get_is_item_worn(id_to_remove))
//			{
//				//*TODO move here the exact removing code from LLWearableBridge::removeItemFromAvatar in the future
//				LLWearableBridge::removeItemFromAvatar(item_to_remove);
//			}
// [SL:KB] - Patch: Appearance-RemoveWearableFromAvatar | Checked: 2010-08-13 (Catznip-3.0.0a) | Added: Catznip-2.1.1d
// [RLVa:KB] - Checked: 2010-09-04 (RLVa-1.2.1c) | Added: RLVa-1.2.1c
			if ( (!rlv_handler_t::isEnabled()) || (gRlvWearableLocks.canRemove(item_to_remove)) )
// [/RLVa:KB]
			{
				const LLWearable* pWearable = gAgentWearables.getWearableFromItemID(item_to_remove->getLinkedUUID());
				if ( (pWearable) && (LLAssetType::AT_BODYPART != pWearable->getAssetType()) )
				{
					U32 idxWearable = gAgentWearables.getWearableIndex(pWearable);
					if (idxWearable < LLAgentWearables::MAX_CLOTHING_PER_TYPE)
					{
						gAgentWearables.removeWearable(pWearable->getType(), false, idxWearable);

						LLAppearanceMgr::instance().removeCOFItemLinks(item_to_remove->getLinkedUUID(), false);
						gInventory.notifyObservers();

// [RLVa:KB] - Checked: 2011-06-07 (RLVa-1.3.1b) | Added: RLVa-1.3.1b
						RlvBehaviourNotifyHandler::onTakeOff(pWearable->getType(), true);
// [/RLVa:KB]
					}
				}
			}
// [/SL:KB]
			break;
		case LLAssetType::AT_OBJECT:
			LLVOAvatarSelf::detachAttachmentIntoInventory(item_to_remove->getLinkedUUID());
		default:
			break;
	}

	// *HACK: Force to remove garbage from COF.
	// Unworn links or objects can't be processed by existed removing functionality
	// since it is not designed for such cases. As example attachment object can't be removed
	// since sever don't sends message _PREHASH_KillObject in that case.
	// Also we can't check is link was successfully removed from COF since in case
	// deleting attachment link removing performs asynchronously in process_kill_object callback.
	removeCOFItemLinks(id_to_remove,false);
}

bool LLAppearanceMgr::moveWearable(LLViewerInventoryItem* item, bool closer_to_body)
{
	if (!item || !item->isWearableType()) return false;
	if (item->getType() != LLAssetType::AT_CLOTHING) return false;
	if (!gInventory.isObjectDescendentOf(item->getUUID(), getCOF())) return false;

	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	LLFindWearablesOfType filter_wearables_of_type(item->getWearableType());
	gInventory.collectDescendentsIf(getCOF(), cats, items, true, filter_wearables_of_type);
	if (items.empty()) return false;

	// We assume that the items have valid descriptions.
	std::sort(items.begin(), items.end(), WearablesOrderComparator(item->getWearableType()));

	if (closer_to_body && items.front() == item) return false;
	if (!closer_to_body && items.back() == item) return false;
	
	LLInventoryModel::item_array_t::iterator it = std::find(items.begin(), items.end(), item);
	if (items.end() == it) return false;


	//swapping descriptions
	closer_to_body ? --it : ++it;
	LLViewerInventoryItem* swap_item = *it;
	if (!swap_item) return false;
	std::string tmp = swap_item->LLInventoryItem::getDescription();
	swap_item->setDescription(item->LLInventoryItem::getDescription());
	item->setDescription(tmp);


	//items need to be updated on a dataserver
	item->setComplete(TRUE);
	item->updateServer(FALSE);
	gInventory.updateItem(item);

	swap_item->setComplete(TRUE);
	swap_item->updateServer(FALSE);
	gInventory.updateItem(swap_item);

	//to cause appearance of the agent to be updated
	bool result = false;
	if ((result = gAgentWearables.moveWearable(item, closer_to_body)))
	{
		gAgentAvatarp->wearableUpdated(item->getWearableType(), FALSE);
	}

	setOutfitDirty(true);

	//*TODO do we need to notify observers here in such a way?
	gInventory.notifyObservers();

	return result;
}

//static
void LLAppearanceMgr::sortItemsByActualDescription(LLInventoryModel::item_array_t& items)
{
	if (items.size() < 2) return;

	std::sort(items.begin(), items.end(), sort_by_description);
}

//#define DUMP_CAT_VERBOSE

void LLAppearanceMgr::dumpCat(const LLUUID& cat_id, const std::string& msg)
{
	LLInventoryModel::cat_array_t cats;
	LLInventoryModel::item_array_t items;
	gInventory.collectDescendents(cat_id, cats, items, LLInventoryModel::EXCLUDE_TRASH);

#ifdef DUMP_CAT_VERBOSE
	llinfos << llendl;
	llinfos << str << llendl;
	S32 hitcount = 0;
	for(S32 i=0; i<items.count(); i++)
	{
		LLViewerInventoryItem *item = items.get(i);
		if (item)
			hitcount++;
		llinfos << i <<" "<< item->getName() <<llendl;
	}
#endif
	llinfos << msg << " count " << items.count() << llendl;
}

void LLAppearanceMgr::dumpItemArray(const LLInventoryModel::item_array_t& items,
										const std::string& msg)
{
	for (S32 i=0; i<items.count(); i++)
	{
		LLViewerInventoryItem *item = items.get(i);
		LLViewerInventoryItem *linked_item = item ? item->getLinkedItem() : NULL;
		LLUUID asset_id;
		if (linked_item)
		{
			asset_id = linked_item->getAssetUUID();
		}
		LL_DEBUGS("Avatar") << self_av_string() << msg << " " << i <<" " << (item ? item->getName() : "(nullitem)") << " " << asset_id.asString() << LL_ENDL;
	}
}

LLAppearanceMgr::LLAppearanceMgr():
	mAttachmentInvLinkEnabled(false),
	mOutfitIsDirty(false),
	mOutfitLocked(false),
	mIsInUpdateAppearanceFromCOF(false)
{
	LLOutfitObserver& outfit_observer = LLOutfitObserver::instance();

	// unlock outfit on save operation completed
	outfit_observer.addCOFSavedCallback(boost::bind(
			&LLAppearanceMgr::setOutfitLocked, this, false));

	mUnlockOutfitTimer.reset(new LLOutfitUnLockTimer(gSavedSettings.getS32(
			"OutfitOperationsTimeout")));

	gIdleCallbacks.addFunction(&LLAttachmentsMgr::onIdle,NULL);
}

LLAppearanceMgr::~LLAppearanceMgr()
{
}

void LLAppearanceMgr::setAttachmentInvLinkEnable(bool val)
{
	llinfos << "setAttachmentInvLinkEnable => " << (int) val << llendl;
	mAttachmentInvLinkEnabled = val;
// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-10-05 (Catznip-3.0.0a) | Added: Catznip-2.2.0a
	if (mAttachmentInvLinkEnabled)
	{
		linkPendingAttachments();
	}
// [/SL:KB]
}

void dumpAttachmentSet(const std::set<LLUUID>& atts, const std::string& msg)
{
       llinfos << msg << llendl;
       for (std::set<LLUUID>::const_iterator it = atts.begin();
               it != atts.end();
               ++it)
       {
               LLUUID item_id = *it;
               LLViewerInventoryItem *item = gInventory.getItem(item_id);
               if (item)
                       llinfos << "atts " << item->getName() << llendl;
               else
                       llinfos << "atts " << "UNKNOWN[" << item_id.asString() << "]" << llendl;
       }
       llinfos << llendl;
}

void LLAppearanceMgr::registerAttachment(const LLUUID& item_id)
{
	   gInventory.addChangedMask(LLInventoryObserver::LABEL, item_id);
// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-10-05 (Catznip-3.0.0a) | Added: Catznip-2.2.0a
	   if (isLinkInCOF(item_id))
	   {
		   return;
	   }
	   mPendingAttachLinks.push_back(item_id);
// [/SL:KB]

	   if (mAttachmentInvLinkEnabled)
	   {
		   // we have to pass do_update = true to call LLAppearanceMgr::updateAppearanceFromCOF.
		   // it will trigger gAgentWariables.notifyLoadingFinished()
		   // But it is not acceptable solution. See EXT-7777
//		   LLAppearanceMgr::addCOFItemLink(item_id, false);  // Add COF link for item.
// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-10-05 (Catznip-3.0.0a) | Modified: Catznip-2.2.0a
		   LLPointer<LLInventoryCallback> cb = new LLRegisterAttachmentCallback();
		   LLAppearanceMgr::addCOFItemLink(item_id, false, cb);  // Add COF link for item.
// [/SL:KB]
	   }
	   else
	   {
		   //llinfos << "no link changes, inv link not enabled" << llendl;
	   }
}

void LLAppearanceMgr::unregisterAttachment(const LLUUID& item_id)
{
	   gInventory.addChangedMask(LLInventoryObserver::LABEL, item_id);
// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-10-05 (Catznip-3.0.0a) | Added: Catznip-2.2.0a
		uuid_vec_t::iterator itPendingAttachLink = std::find(mPendingAttachLinks.begin(), mPendingAttachLinks.end(), item_id);
		if (itPendingAttachLink != mPendingAttachLinks.end())
		{
			mPendingAttachLinks.erase(itPendingAttachLink);
		}
// [/SL:KB]

	   if (mAttachmentInvLinkEnabled)
	   {
		   LLAppearanceMgr::removeCOFItemLinks(item_id, false);
	   }
	   else
	   {
		   //llinfos << "no link changes, inv link not enabled" << llendl;
	   }
}

// [SL:KB] - Patch: Appearance-SyncAttach | Checked: 2010-09-18 (Catznip-3.0.0a) | Modified: Catznip-2.2.0a
void LLAppearanceMgr::linkPendingAttachments()
{
   LLPointer<LLInventoryCallback> cb = NULL;
   for (uuid_vec_t::const_iterator itPendingAttachLink = mPendingAttachLinks.begin(); 
			itPendingAttachLink != mPendingAttachLinks.end(); ++itPendingAttachLink)
	{
		const LLUUID& idAttachItem = *itPendingAttachLink;
		if ( (gAgentAvatarp->isWearingAttachment(idAttachItem)) && (!isLinkInCOF(idAttachItem)) )
		{
			if (!cb)
				cb = new LLRegisterAttachmentCallback();
			LLAppearanceMgr::addCOFItemLink(idAttachItem, false, cb);
		}
	}
}

void LLAppearanceMgr::onRegisterAttachmentComplete(const LLUUID& idItem)
{
	const LLUUID& idItemBase = gInventory.getLinkedItemID(idItem);

	// Remove the attachment from the pending list
	uuid_vec_t::iterator itPendingAttachLink = std::find(mPendingAttachLinks.begin(), mPendingAttachLinks.end(), idItemBase);
	if (itPendingAttachLink != mPendingAttachLinks.end())
		mPendingAttachLinks.erase(itPendingAttachLink);

	// It may have been detached already in which case we should remove the COF link
	if ( (isAgentAvatarValid()) && (!gAgentAvatarp->isWearingAttachment(idItemBase)) )
		removeCOFItemLinks(idItemBase, false);
}
// [/SL:KB]

BOOL LLAppearanceMgr::getIsInCOF(const LLUUID& obj_id) const
{
	return gInventory.isObjectDescendentOf(obj_id, getCOF());
}

// static
bool LLAppearanceMgr::isLinkInCOF(const LLUUID& obj_id)
{
	 LLInventoryModel::cat_array_t cats;
	 LLInventoryModel::item_array_t items;
	 LLLinkedItemIDMatches find_links(gInventory.getLinkedItemID(obj_id));
	 gInventory.collectDescendentsIf(LLAppearanceMgr::instance().getCOF(),
									 cats,
									 items,
	 LLInventoryModel::EXCLUDE_TRASH,
	 find_links);

	 return !items.empty();
}

BOOL LLAppearanceMgr::getIsProtectedCOFItem(const LLUUID& obj_id) const
{
	if (!getIsInCOF(obj_id)) return FALSE;

	// If a non-link somehow ended up in COF, allow deletion.
	const LLInventoryObject *obj = gInventory.getObject(obj_id);
	if (obj && !obj->getIsLinkType())
	{
		return FALSE;
	}

	// For now, don't allow direct deletion from the COF.  Instead, force users
	// to choose "Detach" or "Take Off".
	return TRUE;
	/*
	const LLInventoryObject *obj = gInventory.getObject(obj_id);
	if (!obj) return FALSE;

	// Can't delete bodyparts, since this would be equivalent to removing the item.
	if (obj->getType() == LLAssetType::AT_BODYPART) return TRUE;

	// Can't delete the folder link, since this is saved for bookkeeping.
	if (obj->getActualType() == LLAssetType::AT_LINK_FOLDER) return TRUE;

	return FALSE;
	*/
}

class CallAfterCategoryFetchStage2: public LLInventoryFetchItemsObserver
{
public:
	CallAfterCategoryFetchStage2(const uuid_vec_t& ids,
								 nullary_func_t callable) :
		LLInventoryFetchItemsObserver(ids),
		mCallable(callable)
	{
	}
	~CallAfterCategoryFetchStage2()
	{
	}
	virtual void done()
	{
		llinfos << this << " done with incomplete " << mIncomplete.size()
				<< " complete " << mComplete.size() <<  " calling callable" << llendl;

		gInventory.removeObserver(this);
		doOnIdleOneTime(mCallable);
		delete this;
	}
protected:
	nullary_func_t mCallable;
};

class CallAfterCategoryFetchStage1: public LLInventoryFetchDescendentsObserver
{
public:
	CallAfterCategoryFetchStage1(const LLUUID& cat_id, nullary_func_t callable) :
		LLInventoryFetchDescendentsObserver(cat_id),
		mCallable(callable)
	{
	}
	~CallAfterCategoryFetchStage1()
	{
	}
	virtual void done()
	{
		// What we do here is get the complete information on the items in
		// the library, and set up an observer that will wait for that to
		// happen.
		LLInventoryModel::cat_array_t cat_array;
		LLInventoryModel::item_array_t item_array;
		gInventory.collectDescendents(mComplete.front(),
									  cat_array,
									  item_array,
									  LLInventoryModel::EXCLUDE_TRASH);
		S32 count = item_array.count();
		if(!count)
		{
			llwarns << "Nothing fetched in category " << mComplete.front()
					<< llendl;
			//dec_busy_count();
			gInventory.removeObserver(this);

			// lets notify observers that loading is finished.
			gAgentWearables.notifyLoadingFinished();
			delete this;
			return;
		}

		llinfos << "stage1 got " << item_array.count() << " items, passing to stage2 " << llendl;
		uuid_vec_t ids;
		for(S32 i = 0; i < count; ++i)
		{
			ids.push_back(item_array.get(i)->getUUID());
		}
		
		gInventory.removeObserver(this);
		
		// do the fetch
		CallAfterCategoryFetchStage2 *stage2 = new CallAfterCategoryFetchStage2(ids, mCallable);
		stage2->startFetch();
		if(stage2->isFinished())
		{
			// everything is already here - call done.
			stage2->done();
		}
		else
		{
			// it's all on it's way - add an observer, and the inventory
			// will call done for us when everything is here.
			gInventory.addObserver(stage2);
		}
		delete this;
	}
protected:
	nullary_func_t mCallable;
};

void callAfterCategoryFetch(const LLUUID& cat_id, nullary_func_t cb)
{
	CallAfterCategoryFetchStage1 *stage1 = new CallAfterCategoryFetchStage1(cat_id, cb);
	stage1->startFetch();
	if (stage1->isFinished())
	{
		stage1->done();
	}
	else
	{
		gInventory.addObserver(stage1);
	}
}

void wear_multiple(const uuid_vec_t& ids, bool replace)
{
	LLPointer<LLInventoryCallback> cb = new LLUpdateAppearanceOnDestroy;
	
	bool first = true;
	uuid_vec_t::const_iterator it;
	for (it = ids.begin(); it != ids.end(); ++it)
	{
		// if replace is requested, the first item worn will replace the current top
		// item, and others will be added.
		LLAppearanceMgr::instance().wearItemOnAvatar(*it,false,first && replace,cb);
		first = false;
	}
}

// SLapp for easy-wearing of a stock (library) avatar
//
/*
class LLWearFolderHandler : public LLCommandHandler
{
public:
	// not allowed from outside the app
	LLWearFolderHandler() : LLCommandHandler("wear_folder", UNTRUSTED_BLOCK) { }

	bool handle(const LLSD& tokens, const LLSD& query_map,
				LLMediaCtrl* web)
	{
		LLPointer<LLInventoryCategory> category = new LLInventoryCategory(query_map["folder_id"],
																		  LLUUID::null,
																		  LLFolderType::FT_CLOTHING,
																		  "Quick Appearance");
		LLSD::UUID folder_uuid = query_map["folder_id"].asUUID();
		if ( gInventory.getCategory( folder_uuid ) != NULL )
		{
			LLAppearanceMgr::getInstance()->wearInventoryCategory(category, true, false);

			// *TODOw: This may not be necessary if initial outfit is chosen already -- josh
			gAgent.setGenderChosen(TRUE);
		}

		// release avatar picker keyboard focus
		gFocusMgr.setKeyboardFocus( NULL );

		return true;
	}
};

LLWearFolderHandler gWearFolderHandler;*/
