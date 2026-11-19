#include "Campaign/RTSCampaignInputShield.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
void URTSCampaignInputShield::SynchronizeProperties()
{
 OnMouseButtonDownEvent.BindDynamic(this,&URTSCampaignInputShield::Consume);
 OnMouseButtonUpEvent.BindDynamic(this,&URTSCampaignInputShield::Consume);
 Super::SynchronizeProperties();
}
FEventReply URTSCampaignInputShield::Consume(FGeometry Geometry,const FPointerEvent& Event)
{ return UWidgetBlueprintLibrary::Handled(); }
