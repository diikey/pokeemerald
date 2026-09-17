EventScript_PkmnCenterStatNurse::
	message Text_WelcomeStatNurse
	waitmessage
	multichoice 1, 1, MULTI_STAT_NURSE_SERVICE, FALSE
	switch VAR_RESULT
	case 0, EventScript_StatNurse_NatureChange
	case MULTI_B_PRESSED, EventScript_StatNurse_Goodbye
	end

EventScript_StatNurse_NatureChange::
	special ChoosePartyMon
	goto_if_eq VAR_0x8004, PARTY_NOTHING_CHOSEN, EventScript_StatNurse_Goodbye
	message Text_NatureChange_WhichStatUp
	waitmessage
	multichoice 1, 1, MULTI_NATURE_INCREASE_STAT, FALSE
	switch VAR_RESULT
	case MULTI_B_PRESSED, EventScript_StatNurse_Goodbye
	copyvar VAR_0x8005, VAR_RESULT
	message Text_NatureChange_WhichStatDown
	waitmessage
	multichoice 1, 1, MULTI_NATURE_DECREASE_STAT, FALSE
	switch VAR_RESULT
	case MULTI_B_PRESSED, EventScript_StatNurse_Goodbye
	copyvar VAR_0x8006, VAR_RESULT
	special SetChosenMonNature
	msgbox Text_NatureChange_Done, MSGBOX_DEFAULT
	release
	end

EventScript_StatNurse_Goodbye::
	release
	end

Text_WelcomeStatNurse::
	.string "Welcome! What service are you\n"
	.string "looking for?$"

Text_NatureChange_WhichStatUp::
	.string "Which Stat needs to go up?$"

Text_NatureChange_WhichStatDown::
	.string "Which Stat goes down?$"

Text_NatureChange_Done::
	.string "Your POKéMON's nature has been\n"
	.string "successfully changed!$"
