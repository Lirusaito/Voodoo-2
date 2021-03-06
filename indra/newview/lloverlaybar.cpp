/** 
 * @file lloverlaybar.cpp
 * @brief LLOverlayBar class implementation
 *
 * $LicenseInfo:firstyear=2002&license=viewergpl$
 * 
 * Copyright (c) 2002-2009, Linden Research, Inc.
 * 
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 * 
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution, or
 * online at
 * http://secondlifegrid.net/programs/open_source/licensing/flossexception
 * 
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 * 
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

// Temporary buttons that appear at the bottom of the screen when you
// are in a mode.

#include "llviewerprecompiledheaders.h"

#include "lloverlaybar.h"

#include "aoremotectrl.h"
#include "llaudioengine.h"
#include "importtracker.h"
#include "llrender.h"
#include "llagent.h"
#include "llagentcamera.h"
#include "llbutton.h"
#include "llchatbar.h"
#include "llfocusmgr.h"
#include "llimview.h"
#include "llmediaremotectrl.h"
#include "llpanelaudiovolume.h"
#include "llparcel.h"
#include "lltextbox.h"
#include "llui.h"
#include "llviewercontrol.h"
#include "llviewertexturelist.h"
#include "llviewerjoystick.h"
#include "llviewermedia.h"
#include "llviewermenu.h"	// handle_reset_view()
#include "llviewermedia.h"
#include "llviewerparcelmedia.h"
#include "llviewerparcelmgr.h"
#include "lluictrlfactory.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llvoiceclient.h"
#include "llvoavatarself.h"
#include "llvoiceremotectrl.h"
#include "llmediactrl.h"
#include "llselectmgr.h"
#include "wlfPanel_AdvSettings.h"




#include "llcontrol.h"

// [RLVa:KB]
#include "rlvhandler.h"
// [/RLVa:KB]

#include <boost/foreach.hpp>

//
// Globals
//

LLOverlayBar *gOverlayBar = NULL;

extern S32 MENU_BAR_HEIGHT;
extern ImportTracker gImportTracker;

BOOL LLOverlayBar::sAdvSettingsPopup;
BOOL LLOverlayBar::sChatVisible;

//
// Functions
//



void* LLOverlayBar::createMediaRemote(void* userdata)
{
	LLOverlayBar *self = (LLOverlayBar*)userdata;	
	self->mMediaRemote =  new LLMediaRemoteCtrl ();
	return self->mMediaRemote;
}

void* LLOverlayBar::createVoiceRemote(void* userdata)
{
	LLOverlayBar *self = (LLOverlayBar*)userdata;	
	self->mVoiceRemote = new LLVoiceRemoteCtrl(std::string("voice_remote"));
	return self->mVoiceRemote;
}

void* LLOverlayBar::createAdvSettings(void* userdata)
{
	LLOverlayBar *self = (LLOverlayBar*)userdata;	
	self->mAdvSettings = new wlfPanel_AdvSettings();
	return self->mAdvSettings;
}

void* LLOverlayBar::createAORemote(void* userdata)
{
	LLOverlayBar *self = (LLOverlayBar*)userdata;	
	self->mAORemote = new AORemoteCtrl();
	return self->mAORemote;
}

void* LLOverlayBar::createChatBar(void* userdata)
{
	gChatBar = new LLChatBar();
	return gChatBar;
}

LLOverlayBar::LLOverlayBar()
	:	LLPanel(),
		mMediaRemote(NULL),
		mVoiceRemote(NULL),
		mAORemote(NULL),
		mMusicState(STOPPED),
		mOriginalIMLabel("")
{
	setMouseOpaque(FALSE);
	setIsChrome(TRUE);
	
	mBuilt = false;

	LLCallbackMap::map_t factory_map;
	factory_map["media_remote"] = LLCallbackMap(LLOverlayBar::createMediaRemote, this);
	factory_map["voice_remote"] = LLCallbackMap(LLOverlayBar::createVoiceRemote, this);
	factory_map["Adv_Settings"] = LLCallbackMap(LLOverlayBar::createAdvSettings, this);
	factory_map["ao_remote"] = LLCallbackMap(LLOverlayBar::createAORemote, this);
	factory_map["chat_bar"] = LLCallbackMap(LLOverlayBar::createChatBar, this);
	
	LLUICtrlFactory::getInstance()->buildPanel(this, "panel_overlaybar.xml", &factory_map);
}

bool updateAdvSettingsPopup(const LLSD &data)
{
	LLOverlayBar::sAdvSettingsPopup = gSavedSettings.getBOOL("wlfAdvSettingsPopup");
	gOverlayBar->childSetVisible("AdvSettings_container", !LLOverlayBar::sAdvSettingsPopup);
	gOverlayBar->childSetVisible("AdvSettings_container_exp", LLOverlayBar::sAdvSettingsPopup);
	return true;
}

bool updateChatVisible(const LLSD &data)
{
	LLOverlayBar::sChatVisible = data.asBoolean();
	return true;
}

bool updateAORemote(const LLSD &data)
{
	gOverlayBar->childSetVisible("ao_remote_container", gSavedSettings.getBOOL("EnableAORemote"));	
	return true;
}


BOOL LLOverlayBar::postBuild()
{
	childSetAction("New IM",onClickIMReceived,this);
	childSetAction("Set Not Busy",onClickSetNotBusy,this);
	childSetAction("Mouselook",onClickMouselook,this);
	childSetAction("Stand Up",onClickStandUp,this);
	childSetAction("Cancel TP",onClickCancelTP,this);
 	childSetAction("Flycam",onClickFlycam,this);
	childSetVisible("chat_bar", gSavedSettings.getBOOL("ChatVisible"));

	mCancelBtn = getChild<LLButton>("Cancel TP");
	setFocusRoot(TRUE);
	mBuilt = true;

	mChatbarAndButtons.connect(this,"chatbar_and_buttons");
	mNewIM.connect(this,"New IM");
	mNotBusy.connect(this,"Set Not Busy");
	mMouseLook.connect(this,"Mouselook");
	mStandUp.connect(this,"Stand Up");
	mFlyCam.connect(this,"Flycam");
	mChatBar.connect(this,"chat_bar");
	mVoiceRemoteContainer.connect(this,"voice_remote_container");

	mOriginalIMLabel = mNewIM->getLabelSelected();

	layoutButtons();

	sAdvSettingsPopup = gSavedSettings.getBOOL("wlfAdvSettingsPopup");
	sChatVisible = gSavedSettings.getBOOL("ChatVisible");

	gSavedSettings.getControl("wlfAdvSettingsPopup")->getSignal()->connect(boost::bind(&updateAdvSettingsPopup,_2));
	gSavedSettings.getControl("ChatVisible")->getSignal()->connect(boost::bind(&updateChatVisible,_2));
	gSavedSettings.getControl("EnableAORemote")->getSignal()->connect(boost::bind(&updateAORemote,_2));
	childSetVisible("AdvSettings_container", !sAdvSettingsPopup);
	childSetVisible("AdvSettings_container_exp", sAdvSettingsPopup);
	childSetVisible("ao_remote_container", gSavedSettings.getBOOL("EnableAORemote"));	


	return TRUE;
}

LLOverlayBar::~LLOverlayBar()
{
	// LLView destructor cleans up children
}

// virtual
void LLOverlayBar::reshape(S32 width, S32 height, BOOL called_from_parent)
{
	S32 delta_width = width - getRect().getWidth();
	S32 delta_height = height - getRect().getHeight();

	if (!delta_width && !delta_height && !sForceReshape)
		return;

	LLView::reshape(width, height, called_from_parent);

	if (mBuilt) 
	{
		layoutButtons();
	}
}

void LLOverlayBar::layoutButtons()
{
	LLView* state_buttons_panel = getChildView("state_management_buttons_container");

	if (state_buttons_panel->getVisible())
	{
		U32 required_width=0;
		const child_list_t& view_list = *(state_buttons_panel->getChildList());
		BOOST_FOREACH(LLView* viewp, view_list)
		{
			required_width+=viewp->getRect().getWidth();
		}

		const S32 MAX_BAR_WIDTH = 800;
		//const S32 MAX_BUTTON_WIDTH = 150;

		static LLCachedControl<S32> status_bar_pad("StatusBarPad",10);
		S32 usable_bar_width = llclamp(state_buttons_panel->getRect().getWidth(), 0, MAX_BAR_WIDTH) - (view_list.size()-1) * status_bar_pad;
		F32 element_scale = (F32)usable_bar_width / (F32)required_width;

		// Evenly space all buttons, starting from left
		S32 left = 0;
		S32 bottom = 1;

		BOOST_REVERSE_FOREACH(LLView* viewp, view_list)
		{
			LLRect r = viewp->getRect();
			S32 new_width = r.getWidth() * element_scale;
			//if(dynamic_cast<LLButton*>(viewp))
			//	new_width = llclamp(new_width,0,MAX_BUTTON_WIDTH);
			r.setOriginAndSize(left, bottom, new_width, r.getHeight());
			viewp->setShape(r,false);
			left += viewp->getRect().getWidth() + status_bar_pad;
		}
	}
}

LLButton* LLOverlayBar::updateButtonVisiblity(LLButton* button, bool visible)
{
	if (button && (bool)button->getVisible() != visible)
	{
		button->setVisible(visible);
		sendChildToFront(button);
		moveChildToBackOfTabGroup(button);
	}
	return button;
}

// Per-frame updates of visibility
void LLOverlayBar::refresh()
{
	bool buttons_changed = FALSE;

	if(LLButton* button = updateButtonVisiblity(mNewIM,gIMMgr->getIMReceived()))
	{
		int unread_count = gIMMgr->getIMUnreadCount();
		if (unread_count > 0)
		{
			if (unread_count > 1)
			{
				std::stringstream ss;
				ss << unread_count << " " << getString("unread_count_string_plural");
				button->setLabel(ss.str());
			}
			else
			{
				button->setLabel("1 " + mOriginalIMLabel);
			}
		}
		buttons_changed = true;
	}
	buttons_changed |= updateButtonVisiblity(mNotBusy,gAgent.getBusy()) != NULL;
	buttons_changed |= updateButtonVisiblity(mFlyCam,LLViewerJoystick::getInstance()->getOverrideCamera()) != NULL;
	buttons_changed |= updateButtonVisiblity(mMouseLook,gAgent.isControlGrabbed(CONTROL_ML_LBUTTON_DOWN_INDEX)||gAgent.isControlGrabbed(CONTROL_ML_LBUTTON_UP_INDEX)) != NULL;
// [RLVa:KB] - Checked: 2009-07-10 (RLVa-1.0.0g)
//  buttons_changed |= updateButtonVisiblity("Stand Up", isAgentAvatarValid() && gAgentAvatarp->isSitting()) != NULL;
	buttons_changed |= updateButtonVisiblity(mStandUp,isAgentAvatarValid() && gAgentAvatarp->isSitting() && !gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) != NULL;
// [/RLVa:KB]
	buttons_changed |= updateButtonVisiblity(mCancelBtn,(gAgent.getTeleportState() >= LLAgent::TELEPORT_START) &&	(gAgent.getTeleportState() <= LLAgent::TELEPORT_MOVING)) != NULL;

	moveChildToBackOfTabGroup(mAORemote);
	moveChildToBackOfTabGroup(mMediaRemote);
	moveChildToBackOfTabGroup(mVoiceRemote);

	// turn off the whole bar in mouselook
	static BOOL last_mouselook = FALSE;

	BOOL in_mouselook = gAgentCamera.cameraMouselook();

	if(last_mouselook != in_mouselook)
	{
		last_mouselook = in_mouselook;
		if (in_mouselook)
		{
			childSetVisible("media_remote_container", FALSE);
			childSetVisible("voice_remote_container", FALSE);
			childSetVisible("AdvSettings_container", FALSE);
			childSetVisible("AdvSettings_container_exp", FALSE);
			childSetVisible("ao_remote_container", FALSE);
			childSetVisible("state_management_buttons_container", FALSE);
		}
		else
		{
			// update "remotes"
			childSetVisible("media_remote_container", TRUE);
			childSetVisible("voice_remote_container", LLVoiceClient::voiceEnabled());
			childSetVisible("AdvSettings_container", !sAdvSettingsPopup);//!gSavedSettings.getBOOL("wlfAdvSettingsPopup")); 
			childSetVisible("AdvSettings_container_exp", sAdvSettingsPopup);//gSavedSettings.getBOOL("wlfAdvSettingsPopup")); 
			childSetVisible("ao_remote_container", gSavedSettings.getBOOL("EnableAORemote"));
			childSetVisible("state_management_buttons_container", TRUE);
		}
	}
	if(!in_mouselook)
		mVoiceRemoteContainer->setVisible(LLVoiceClient::voiceEnabled());

	// always let user toggle into and out of chatbar
	static const LLCachedControl<bool> chat_visible("ChatVisible",true);
	mChatBar->setVisible(chat_visible);

	if (buttons_changed)
	{
		layoutButtons();
	}
}

//-----------------------------------------------------------------------
// Static functions
//-----------------------------------------------------------------------

// static
void LLOverlayBar::onClickIMReceived(void*)
{
	gIMMgr->setFloaterOpen(TRUE);
}


// static
void LLOverlayBar::onClickSetNotBusy(void*)
{
	gAgent.clearBusy();
}


// static
void LLOverlayBar::onClickFlycam(void*)
{
	LLViewerJoystick::getInstance()->toggleFlycam();
}

// static
void LLOverlayBar::onClickResetView(void* data)
{
	handle_reset_view();
}

//static
void LLOverlayBar::onClickMouselook(void*)
{
	gAgentCamera.changeCameraToMouselook();
}

//static
void LLOverlayBar::onClickStandUp(void*)
{
// [RLVa:KB] - Checked: 2009-07-10 (RLVa-1.0.0g)
	if ( (gRlvHandler.hasBehaviour(RLV_BHVR_UNSIT)) && (gAgentAvatarp) && (gAgentAvatarp->isSitting()) )
	{
		return;
	}
// [/RLVa:KB]

	LLSelectMgr::getInstance()->deselectAllForStandingUp();
	gAgent.setControlFlags(AGENT_CONTROL_STAND_UP);
}

//static
void LLOverlayBar::onClickCancelTP(void* data)
{
	LLOverlayBar* self = (LLOverlayBar*)data;
	self->setCancelTPButtonVisible(FALSE, std::string("Cancel TP"));
	gAgent.teleportCancel();
	llinfos << "trying to cancel teleport" << llendl;
}

void LLOverlayBar::setCancelTPButtonVisible(BOOL b, const std::string& label)
{
	mCancelBtn->setVisible( b );
//	mCancelBtn->setEnabled( b );
	mCancelBtn->setLabelSelected(label);
	mCancelBtn->setLabelUnselected(label);
}


////////////////////////////////////////////////////////////////////////////////
void LLOverlayBar::audioFilterPlay()
{
	if (gOverlayBar && gOverlayBar->mMusicState != PLAYING)
	{
		gOverlayBar->mMusicState = PLAYING;
	}
}

void LLOverlayBar::audioFilterStop()
{
	if (gOverlayBar && gOverlayBar->mMusicState != STOPPED)
	{
		gOverlayBar->mMusicState = STOPPED;
	}
}

////////////////////////////////////////////////////////////////////////////////
// static media helpers
// *TODO: Move this into an audio manager abstraction
//static
void LLOverlayBar::mediaStop(void*)
{
	if (!gOverlayBar)
	{
		return;
	}
	LLViewerParcelMedia::stop();
}
//static
void LLOverlayBar::toggleMediaPlay(void*)
{
	if (!gOverlayBar)
	{
		return;
	}

	
	if (LLViewerParcelMedia::getStatus() == LLViewerMediaImpl::MEDIA_PAUSED)
	{
		LLViewerParcelMedia::start();
	}
	else if(LLViewerParcelMedia::getStatus() == LLViewerMediaImpl::MEDIA_PLAYING)
	{
		LLViewerParcelMedia::pause();
	}
	else
	{
		LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
		if (parcel)
		{
			LLViewerParcelMedia::sIsUserAction = true;
			LLViewerParcelMedia::play(parcel);
		}
	}
}

//static
void LLOverlayBar::toggleMusicPlay(void*)
{
	if (!gOverlayBar)
	{
		return;
	}
	
	if (gOverlayBar->mMusicState != PLAYING)
	{
		gOverlayBar->mMusicState = PLAYING; // desired state
		if (gAudiop)
		{
			LLParcel* parcel = LLViewerParcelMgr::getInstance()->getAgentParcel();
			if ( parcel )
			{
				// this doesn't work properly when crossing parcel boundaries - even when the 
				// stream is stopped, it doesn't return the right thing - commenting out for now.
	// 			if ( gAudiop->isInternetStreamPlaying() == 0 )
				{
					LLViewerParcelMedia::sIsUserAction = true;
					LLViewerParcelMedia::playStreamingMusic(parcel);
				}
			}
		}
	}
	//else
	//{
	//	gOverlayBar->mMusicState = PAUSED; // desired state
	//	if (gAudiop)
	//	{
	//		gAudiop->pauseInternetStream(1);
	//	}
	//}
	else
	{
		gOverlayBar->mMusicState = STOPPED; // desired state
		if (gAudiop)
		{
			gAudiop->stopInternetStream();
		}
	}
}

