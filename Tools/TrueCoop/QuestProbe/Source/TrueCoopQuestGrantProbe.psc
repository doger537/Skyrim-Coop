Scriptname TrueCoopQuestGrantProbe extends Quest

; Diagnostic only. Never attach this script to a normal quest or a saved game.
Function GrantForAudit()
    Actor recipient = Game.GetPlayer()
    Form gold = Game.GetForm(15)
    Int before = recipient.GetItemCount(gold)
    recipient.AddItem(gold, 17, True)
    Int after = recipient.GetItemCount(gold)
    Debug.Trace("TRUECOOP_QUEST_PROBE before=" + before + " after=" + after + " delta=" + (after - before))
    Debug.Notification("TrueCoop quest probe: gold delta " + (after - before))
EndFunction
