# Smart HRV  
Smart HRV replaces OEM controllers for Carrier (and probably Venmar and other brand) Heat Recovery Ventilation  
units.  Smart HRV adds intelligence to system controls using indoor and outdoor sensors to montior humidity and  
to manage humidity, fresh air, and odor control optimal performance. Smart HRV also integrates with fan and dehumidifier  
controls on Nest thermostats.  The system consists of an arduino-based control unit, one or more indoor humidity sensors.  
An outdoor temperature and humidity sensor, and an android user interface app to provide monitoring and control of system   
functions.  
  
The current system is customized to our own application and will likely need modification for other applications. But the   
package includes basic serial control logic for controlling an HRV and hopefully can provide guidance for others wishing  
to improve permormance of their heat recovery ventialation systems.  
  
![](/images/smart_hrv_controller.png "Smart HRV Controller")  
  
# Background  
HRV and ERV (Energy Recovery Ventillator) have three basic functions:  
  
1. Manage indoor air quality efficiently by exchange fresh outdoor air for stale indoor air while recovering a portion of the heat from the exhaust airstream to conserve energy. 
 
2. Manage indoor humidity to reduce damage around windows and other cold surfaces by exchanging moist indoor air with dryer outdoor air; again recovering a portion of the heat from the exhaust air stream.  

3. Manage odors by exhausting indoor air.  
  
HRV/ERV systems work very well but the controls that came with ours (and most other units) fail on all three of their intended tasks.  Many units like ours lack any sort of timer circuit to allow fresh air exchange to be cycled. Instead, manufacturers and many HVAC expert claim the unit should be run continuously to maintain a steady flow of fresh air. This is insane! Heat recovery efficiency ranges from around 60%-70% for HRV units like ours, to about 90% for the newest ERV units. That means for every cubic foot of air exhausted by our unit, we lose 40% of the heat it contains. Heat we paid dearly to produce! Sure, it's better than opening a window but cycling the unit can provide plenty of fresh air while saving a significant amount of heat. Second, the best humidity to keep your house in winter depends on the outside temperature. The colder it is outside, the more likely moisture is to condense on windows so you need lower indoor humidity to prevent it. Also, the ability to reduce humidity by echanging indoor and outdoor air depends on the humidity outside. Luckily, outdoor humidity drops with the temperatue so on cold days, there is a good chance it is dryer outside than inside. But still, basic HRV units have no way to know if exchanging air will help and they could run in futility without reducing indoor humidity on wet days. Basic controllers have a manual dial setting to adjust the target indoor humidity. But outdoor temperatures fluctuate wildly over the day and season and nobody has the time to constantly fiddle with the dial to guess what the weather is going to do. This leads to settings that either run too little or too much so wind up with condensation damage on the windows anyway, wasted heat loss, or both. Finally, HRV/ERV units often include manual push buttons installed in bathrooms and laundry rooms that run for a set time when humidity or odor control is needed. OUrs ran for 20 minutes on a push which was not enough time to eliminate humidity after a shower, and certainly not enough time to eliminate odors when the relatives are visiting.

After years of frustration and some band-aid fixes for these inadequate controls, the catalyst to do something came when I purchased several Nest thermostats for our house and realized they provided fan and dehumidifier control functions that could improve the situation.  The fan control provided adjustable fresh air cycling and the dehumidifier provided the possibility of continuously monitoring outdoor temps and updating the humidity setpoint to always have the HRV trying to achieve the optimal humidity. I thought it would be just a matter of throwing in a couple of relays to get the Nest talking to the HRV but I was wrong.  There is no provision for such a setup so the only good option was to hack the controller.  So I set about reverse engineering the serial ttl communication logic, tossing out the old ugly wall controller and replacing it with an arduino controller interfaced with a cheap used android tablet mounted on the wall. The result is the Smart HRV system which has been chugging along with continuous tweaks for about two years now.
  
# Features: 

1. Monitors indoor humidity in multiple rooms and outdoor temperature and humidity to automatically and intelligently determine optimal inddor humidity levels and initiate fresh air exchange or recirculation cycles to achieve target humidity with minimal heat loss.

2. Integrates with Nest thermostat to allow fan controls on thermostat to initiate low speed air exchange cycles.

3. Defrost indicator on user interface to show when a defrost recirculating cycle is active.

4. Tracks cumulative run time and alerts users when it is time to service filters.  
  
  
# Extras:  
  

  
# Error checking and logging:   

  
  
   
   
  