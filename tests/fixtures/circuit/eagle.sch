<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE eagle SYSTEM "eagle.dtd">
<eagle version="9.6.2"><drawing><settings/><grid distance="0.1" unitdist="inch"/>
<layers><layer number="91" name="Nets" color="2" fill="1" visible="yes" active="yes"/></layers>
<schematic><libraries><library name="Fixture"><symbols><symbol name="AMP">
<wire x1="-2" y1="-2" x2="3" y2="0" width="0.2" layer="94"/>
<wire x1="3" y1="0" x2="-2" y2="2" width="0.2" layer="94"/>
<wire x1="-2" y1="2" x2="-2" y2="-2" width="0.2" layer="94"/>
<pin name="IN" x="-7.08" y="0" visible="both" length="middle" direction="in"/>
<pin name="OUT" x="8.08" y="0" visible="both" length="middle" direction="out" rot="R180" function="dotclk"/>
<text x="-2" y="3" size="1.27" layer="95">&gt;NAME</text>
<text x="-2" y="-4" size="1.27" layer="96">&gt;VALUE</text>
<text x="-2" y="-6" size="1.27" layer="96">&gt;RATING</text>
</symbol></symbols><devicesets><deviceset name="AMP" prefix="U"><gates>
<gate name="A" symbol="AMP" x="0" y="0"/><gate name="B" symbol="AMP" x="10" y="0"/>
</gates><devices><device name="" package="TEST"><connects>
<connect gate="A" pin="IN" pad="1"/><connect gate="A" pin="OUT" pad="2"/>
<connect gate="B" pin="IN" pad="3"/><connect gate="B" pin="OUT" pad="4"/>
</connects></device></devices></deviceset></devicesets></library></libraries>
<parts><part name="U1" library="Fixture" deviceset="AMP" device="" value="Dual &amp; amplifier"><attribute name="RATING" value="3.3 V"/></part></parts>
<sheets><sheet><plain>
<text x="10" y="35" size="1.27" layer="97">Authored EAGLE fixture</text>
<wire x1="10" y1="10" x2="20" y2="20" width="0.2" layer="94" curve="90"/>
<circle x="40" y="10" radius="2" width="0.2" layer="94"/>
<rectangle x1="45" y1="8" x2="49" y2="12" layer="94" rot="R45"/>
<polygon width="0" layer="94"><vertex x="55" y="8"/><vertex x="59" y="8"/><vertex x="57" y="12"/></polygon>
</plain><instances><instance part="U1" gate="A" x="20" y="25" smashed="no"/>
<instance part="U1" gate="B" x="50" y="25" smashed="yes" rot="MR90">
<attribute name="NAME" x="54" y="30" size="1.27" layer="95"/>
<attribute name="VALUE" x="54" y="28" size="1.27" layer="96"/>
</instance></instances><nets><net name="SIGNAL" class="0"><segment>
<pinref part="U1" gate="A" pin="IN"/>
<wire x1="8" y1="25" x2="12.92" y2="25" width="0.15" layer="91"/>
<junction x="8" y="25"/><label x="8" y="25" size="1.27" layer="95"/>
</segment></net></nets></sheet>
<sheet><plain><text x="10" y="10" size="1.27" layer="97">Second sheet</text>
<frame x1="0" y1="0" x2="60" y2="40" columns="-3" rows="2" layer="97"/>
<text x="10" y="7" size="1.27" layer="97">&gt;SHEET</text>
</plain>
<instances/><busses><bus name="DATA[0..7]"><segment><wire x1="10" y1="20" x2="30" y2="20" width="0.3" layer="92"/>
<label x="10" y="20" size="1.27" layer="95"/></segment></bus></busses></sheet>
</sheets></schematic></drawing></eagle>
