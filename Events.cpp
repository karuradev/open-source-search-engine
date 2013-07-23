//-*- coding: utf-8 -*-

//////////
//
// THE TWO MAIN THINGS WITH EVENTS:
//
// 1. ADDING IMPLIED SECTIONS
// 2. EVENT LIST DECTECTION (SEC_EVENT_BROTHER bit)
//
//////////

#include "Events.h"
#include "Address.h"
#include "gb-include.h"
#include "Sections.h"
#include "Dates.h"
#include "Abbreviations.h"
#include "Phrases.h"
//#include "Weights.h"
#include "XmlDoc.h" // hashWords()
#include "Hostdb.h"
#include "Address.h"
#include "sort.h"
#include "Test.h"
#include "Timedb.h"
#include "Repair.h"

// TODO: create faux sections for http://www.dailylobo.com/calendar/ so
//       we can parse its events

// TODO: grab image thumbnail next to events!!

// TODO: do a site: on site to see what places the site is primarily concerned

// TODO: check out http://en.wikipedia.org/wiki/Address_(geography)
//       for address formats from country to country

// TODO: grab titles from wikipedia that have lat/long because there seem
//       to be a lot not in allCountries.txt

// TODO: get abbreviations from wikipedia.

// TODO: get foreign equivalents for place indicators from wikiepdia
//       i.e. strasse is street in german...

// TODO: address examples here: http://www.columbia.edu/kermit/postal.html

// TODO: get all foreign alternative names from wikipedia's outlinks
//       on the page for that particular place/location


void addEventIds ( Section *dst , Section *src ) ;

class Sections *g_sections2 = NULL;

int dateseccmp ( const void *arg1 , const void *arg2 ) {
	// get the dates
	Date *date1 = *(Date **)arg1;
	Date *date2 = *(Date **)arg2;
	// get word position
	//long a1 = date1->m_section->m_a;
	//long a2 = date2->m_section->m_a;
	long a1 = date1->m_a;
	long a2 = date2->m_a;
	// sanity check
	if ( a1 < 0 ) { char *xx=NULL;*xx=0; }
	if ( a2 < 0 ) { char *xx=NULL;*xx=0; }
	// get sections
	long sa1 = g_sections2->m_sectionPtrs[a1]->m_a;
	long sa2 = g_sections2->m_sectionPtrs[a2]->m_a;
	// compare
	return ( sa1 - sa2);
}

#define MAX_GOOD 1000

// . returns false and sets g_errno on error
bool Events::set ( Url         *u         ,
		   Words       *ww        ,
		   Xml         *xml       ,
		   Links       *links     ,
		   Phrases     *phrases   ,
		   Synonyms    *synonyms  ,
		   //Weights     *weights   ,
		   Bits        *bits      ,
		   Sections    *sections  ,
		   SubSent     *subSents  ,
		   long         numSubSents,
		   SectionVotingTable *osvt ,
		   Dates       *dates     ,
		   Addresses   *addresses ,
		   TagRec      *gr        ,
		   XmlDoc      *xd        ,
		   long         spideredTime ,
		   SafeBuf     *pbuf      ,
		   long         niceness  ) {

	m_eventDataValid = false;
	m_isStubHubValid = false;

	// save for calling Events::hash
	m_words    = ww;
	m_xml      = xml;
	m_links    = links;
	m_phrases  = phrases;
	m_synonyms = synonyms;
	//m_weights  = weights;
	m_niceness = niceness;
	m_sections = sections;
	m_subSents = subSents;
	m_numSubSents = numSubSents;
	m_osvt     = osvt;
	m_dates    = dates;
	m_xd       = xd;
	m_bitsOrig = bits;
	m_bits     = bits->m_bits;
	m_addresses= addresses;
	m_pbuf     = pbuf;

	m_wptrs    = m_words->getWords();
	m_wlens    = m_words->getWordLens();
	m_wids     = m_words->m_wordIds;
	m_tids     = m_words->m_tagIds;
	m_nw       = m_words->m_numWords;
	m_url      = u;

	m_isFacebook = false;
	if ( strncmp(m_url->m_url,"http://www.facebook.com/event",29)==0 )
		m_isFacebook = true;

	// use this instead of clock now
	m_spideredTime = spideredTime;
	// shortcut
	Sections *ss = m_sections;

	// reset
	m_numEvents = 0;
	//m_numValidEvents = 0;
	//m_revisedValid = 0;
	m_numFutureDates = 0;
	m_numRecurringDates = 0;
	m_numTODs = 0;

	//m_regTableValid = false;
	//m_setRegBits = false;

	m_note      = NULL;

	if ( ! ww ) return true;

	// javascript?
	if ( xd->m_contentTypeValid &&
	     xd->m_contentType == CT_JS )
		return true;

	//if ( strstr(u->m_url,"www.collectorsguide.com/ab/abmud.html") ) {
	//	log("GOTIT");
	//}

	// shortcuts
	//long long *wids  = ww->getWordIds  ();
	//nodeid_t   *tids = ww->getTagIds();
	Section     **sp = ss->m_sectionPtrs;
	//long          ns = ss->m_numSections;

	///////////////////////////////////////////
	//
	// Count the number of future or recurring dates we have
	//
	// . need at least one to have events
	// . our time now in UTC (time is from host #0's clock)
	// . now because we re-inject docs in the test subdir to check 
	//   parsing changes we need to keep things exactly the same, so
	//   use the spidered date of the doc
	//
	//////////////////////////////////////////////
	long now = m_spideredTime; // getTimeGlobal();
	// shortcut
	long nd = m_dates->m_numDatePtrs;
	// count good future dates
	long future = 0;
	// count recurring dates
	long recurring = 0;
	// scan our dates
	for ( long i = 0 ; i < nd ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// . skip if bad format
		// . no, assume american because address is only in america
		// . no, now we resolve this if Dates.cpp if possible, 
		//   but if this flag is set we could not find a clear
		//   resolution for the date!!
		if ( flags & DF_AMBIGUOUS ) continue;
		// or clock
		if ( flags & DF_CLOCK ) continue;
		// must be from body
		if ( ! (flags & DF_FROM_BODY) ) continue;
		// must not be fuzzy
		if ( flags & DF_FUZZY ) continue;
		// is also part of verified/inlined address?
		if ( m_bits[di->m_a] & D_IS_IN_ADDRESS ) continue;
		if ( m_bits[di->m_a] & D_IS_IN_VERIFIED_ADDRESS_NAME) continue;
		// must not be a pub date
		if ( flags & DF_PUB_DATE ) continue;
		// or ticket date
		if ( flags & DF_NONEVENT_DATE ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// do not count "evenings [[]] 9pm" crap
		// or "summer [[]] 9pm"
		if ( !(di->m_hasType & DT_DOW) &&
		     !(di->m_hasType & DT_DAYNUM) &&
		     !(di->m_hasType & DT_HOLIDAY) &&
		     !(di->m_hasType & DT_SUBWEEK) &&
		     !(di->m_hasType & DT_SUBMONTH) &&
		     !(di->m_hasType & DT_EVERY_DAY) &&
		     !(di->m_hasType & DT_TIMESTAMP) )
			continue;
		// must not be a bad recurring dow
		//if ( flags & DF_BAD_RECURRING_DOW ) continue;
		// count these for XmlDoc::hasTOD()
		if ( (di->m_hasType & DT_TOD) && 
		     // only count if it is recurring, otherwise it will be
		     // counted for the future count below. we will set the
		     // bit SpiderReply::m_hasTOD.
		     (di->m_timestamp == 0 ) ) 
			m_numTODs++;
		// if no year, it is recurring
		if ( di->m_timestamp == 0 ) recurring++;
		// ignore if it is just a time of day (i.e. "5pm")
		//if ( flags & DF_TOD ) continue;
		// must have a year
		//if ( ! ( flags & DF_HAS_YEAR ) ) continue;
		// must be within 24 hrs from now or later
		long age = now - di->m_timestamp;
		// check it. must be 24 hrs old or less
		if ( age > 24 * 3600 ) continue;
		// reset si
		Section *si = m_sections->m_sectionPtrs[di->m_a];
		// skip if voted as clock by more than half voters
		if ( m_osvt->getScore(si,SV_CLOCK) > .5 ) continue;
		// count it
		future++;
	}
	// save this for setting SpiderReply::m_hasFutureDaynum
	m_numFutureDates    = future;
	m_numRecurringDates = recurring;

	// no address means, no events
	if ( addresses->m_am.getNumPtrs() <= 0 ) return true; // m_na

	// need at least 1 "future" or now date to be considered to have events
	if ( future <= 0 && recurring <= 0 ) return true;

	// there are bad section flags
	sec_t badFlags = SEC_MARQUEE|SEC_SELECT|SEC_SCRIPT|SEC_STYLE|
		SEC_NOSCRIPT|SEC_HIDDEN;
	// . zevents.com has some dates and titles each in their own link
	// . we end up missing the title because SEC_MENU is set for it
	//   as well as the event date, so this fixes that, by just ignoring
	//   such dates
	badFlags |= SEC_MENU;

	// . do not consider events from menu sections
	// . we set SEC_DUP in Sections.cpp to sections whose content
	//   hash is repeated on other web pages from this site
	// . no! let's add these and they will automatically be labelled
	//   as bad events since SEC_DUP is in the SEC_BAD_EVENT flags list
	//badFlags |= SEC_DUP;


	/////////////////////////////////////////////
	//
	// . sort the dates we will use for events
	// . right now the dates are sorted by their word position, Date::m_a
	// . but we need to sort them by their Section::m_a
	// . usually these two things give the same ordering but occassionaly
	//   section A contains section B and the date in section B is after
	//   the date in section
	//
	/////////////////////////////////////////////
	char dbuf[10000];
	Date **dsorted = (Date **)dbuf;
	long nds = 0;
	long dneed = nd * 4;
	if ( dneed > 10000 ) {
		dsorted = (Date **)mmalloc(dneed,"datesort");
		if ( ! dsorted ) return false;
	}
	// fill it up
	for ( long i = 0 ; i < nd ; i++ ) {
		// give up control
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// stop when we run into the telescoped dates
		//if ( di->m_type == DT_TELESCOPE ) break;
		// . ignore if isolated year, that may telescope out
		// . stops "2009 [[]] ..." to trumba rss feed page
		if ( di->m_type == DT_YEAR ) continue;

		if ( di->m_flags5 & DF5_IGNORE ) continue;

		// this is always good (Facebook timestamp dates)
		if ( di->m_hasType == (DT_TIMESTAMP|DT_COMPOUND) ) {
			di->m_flags |= DF_EVENT_CANDIDATE;
			dsorted[nds++] = di;
			continue;
		}
		if ( di->m_hasType == (DT_TIMESTAMP|DT_COMPOUND|DT_RANGE) ) {
			di->m_flags |= DF_EVENT_CANDIDATE;
			dsorted[nds++] = di;
			continue;
		}

		// official date is the new facebook
		if ( di->m_flags & DF_OFFICIAL ) {
			di->m_flags |= DF_EVENT_CANDIDATE;
			dsorted[nds++] = di;
			continue;
		}

		// or year range...
		//if ( di->m_hasType == DT_YEAR ) continue;....
		// . use the telescoped version of this date if it exists
		// . do not even continue. we got a store hours date 
		//   which we want to use, and it has a bunch of bad telescopes
		//if ( di->m_telescope ) continue;//di = di->m_telescope;
		// this was causing things that weren't really store hours
		// to be labelled as such...
		if ( di->m_telescope && !(di->m_flags & DF_STORE_HOURS) )
			continue;

		// if they are both store hours in telescope...
		// fixes "7:30 am - 10 am Sun [[]] Summer Hours: "
		// "March 15 - Oct. 15" for unm.edu, where both
		// of those are in the store hours section and considered
		// a full store hours.
		if ( di->m_telescope &&
		     (di->m_telescope->m_flags & DF_STORE_HOURS) )
			continue;

		// . or likewise both part of the same weekly schedule
		// . we set DF_WEEKLY_SCHEDULE for dates that are like
		//   store hours but do not have the required number of
		//   open hours per week. (28 hrs)
		// . but they do have multiple dows, etc.
		if ( di->m_telescope &&
		     (di->m_telescope->m_flags & DF_WEEKLY_SCHEDULE ) )
			continue;

		// stop if "last modified" preceeds it
		if ( di->m_flags & DF_PUB_DATE ) continue;
		// or ticket date
		if ( di->m_flags & DF_NONEVENT_DATE ) continue;
		//if ( di->m_flags5 & DF5_IGNORE ) continue;
		// skip if dup
		if ( di->m_flags & DF_DUP ) continue;
		// or ends on something bad
		//if ( di->m_flags & DF_BAD_ENDING ) continue;

		// crap graffiti.org had the month daynum range just
		// outside of the store hours section, the section that
		// contained both the store hours and address. so we
		// ended up thinking the exhibits was the store description.
		// therefore, let's no longer allow store hours if it is
		// a subdate...
		if ( di->m_flags & DF_SUB_DATE )
			continue;

		// only store hours can be sub dates and event candidates
		if ( (di->m_flags & DF_SUB_DATE) &&
		     !(di->m_flags & DF_STORE_HOURS ) )
			continue;

		// . but if the "super" date is also store hours then you
		//   can't be a subdate.
		// . fixes "4:00PM-12:00AM [[]] Wednesday" being an event
		//   candidate even though
		//   "Wednesday [[]] 4:00PM-12:00AM [[]] November 23 - 27 "
		//    on Monday 4:00PM-12:00AM" is also store hours...
		// . for http://www.publicbroadcasting.net/
		if ( (di->m_flags & DF_SUB_DATE) &&
		     (di->m_subdateOf->m_flags & DF_STORE_HOURS) )
			continue;


		// . fix "Every Tuesday [[]] 7:30PM - 11:30PM" so it is not 
		//   discard because it is a subdate of 
		//   "Every Tuesday [[]] before 9PM [[]] 7:30PM - 11:30PM"
		// . So if what we are a subdate of is exactly like us except
		//   it has something like "[[]] before 9PM" then we should
		//   both be able to co-exist.
		/*
		if ( (di->m_flags & DF_SUB_DATE) &&
		     // if he has additional "[[]] before 9PM"
		     (di->m_subdateOf->m_flags & DF_ONGOING) &&
		     // and we do not
		     !(di->m_flags & DF_ONGOING) &&
		     // and otherwise we are exactly the same
		     di->m_hasType == di->m_subdateOf->m_hasType )
			// then allow us in
			continue;
		*/   
		// skip if upside down
		//if ( di->m_flags & DF_UPSIDEDOWN ) continue;
		// . a time of day is required for an event, whether inferred
		//   or implied (all day, store hours, etc.)
		//   this fixes "December 10, 2009 - January 3, 2010" from
		//   making an event for santefeplayhouse.com tuna christmas
		// . should also support burtstikilounge.com url so a
		//   daynum in a calendar can telescope to the store hours
		//   and we can count that has having a tod
		if ( ! ( (di->m_hasType) & DT_TOD ) ) continue;
		// do not count "evenings [[]] 9pm" crap
		// or "summer [[]] 9pm"
		if ( !(di->m_hasType & DT_DOW) &&
		     !(di->m_hasType & DT_DAYNUM) &&
		     !(di->m_hasType & DT_HOLIDAY) &&
		     !(di->m_hasType & DT_SUBWEEK) &&
		     !(di->m_hasType & DT_SUBMONTH) &&
		     !(di->m_hasType & DT_EVERY_DAY) )
			continue;
		/*

		  NOTE: these checks now handled by the one below

		// skip "Tuesday 1:00PM [[]] Feb" in
		// http://www.zipscene.com/events/view/2848438-2-75-u-call-its
		// -with-dj-johnny-b-cincinnati
		if ( ! (di->m_hasType & DT_DAYNUM) &&
		       (di->m_hasType & DT_MONTH) &&
		       (di->m_hasType & DT_DOW) &&
		     !(di->m_suppFlags & SF_RECURRING_DOW) )
			continue;

		// skip "Wednesday/Thursday [[]] 8:00 a.m" for
		// http://www.uillinois.edu/trustees/meetings.cfm
		if ( ! (di->m_hasType & DT_DAYNUM) &&
		     ! (di->m_hasType & DT_RANGE_TOD) &&
		     ! (di->m_hasType & DT_RANGE_DOW) &&
		       (di->m_hasType & DT_DOW) &&
		     !(di->m_suppFlags & SF_RECURRING_DOW) )
			continue;
		*/

		// treat "6:30 am. Sat. and Sun.only" as recurring to fix 
		// unm.edu, but do not allwo telescopes to prevent
		// "Wed/Thu [[]] 8am" for uillinois.edu from being an event.
		// TODO: really this should have DF_WEEKLY_SCHEDULE set 
		// for unm.edu... however that only does tod ranges for now
		bool impliedRecurring = false;
		if ( !(di->m_hasType & DT_TELESCOPE) &&
		     (di->m_hasType & DT_LIST_DOW) &&
		     (di->m_hasType & DT_TOD) )
			impliedRecurring = true;

		// fix "Mon [[]] 4-4:30pm"
		// above we were assuming a tod range would constitute store
		// hours, even if it was only for a single DOW. however,
		// we can't do that any more because https://mercury.intouch-
		// usa.com/webpoint/wp.dll?axis/iceland is a calendar format
		// and its "April 2011" is in a menu so it is not used. so
		// we have to assume NOT store hours. this will hurt us for
		// other pages UNLESS we set SF_RECURRING_DOW bit by checking
		// for keywords like "hours:" or by seeing if it is brothers
		// with a DOW range date. 
		//
		// need a month/daynum or holiday
		if ((di->m_hasType&(DT_MONTH|DT_DAYNUM))!=
		    (DT_MONTH|DT_DAYNUM)&&
		    // but a holiday would do, acts like a month/daynum
		    !(di->m_hasType & DT_HOLIDAY) &&
		    // having a dow range is kinda like recurring
		    !(di->m_hasType & DT_RANGE_DOW) &&
		    // . or a list of dows is kinda like recurring
		    // . fixes "11:30 am.-noon Sat. and Sun. only" for unm.edu
		    // . crap but hurts uillinois.edu and causes 
		    //   "Wed/Thu [[]] 8am" cuz it thinks it is recurring
		    // . so just make this more specific
		    ! impliedRecurring &&
		    // or part of a weekly schedule but not store hours
		    // because not open long enough over the week. so 
		    // if they have "mon-fri 8am-9am" and "sat 9am-10am" then
		    // "sat 9am-10am" will still be seen as a recurring event
		    !(di->m_flags & DF_WEEKLY_SCHEDULE) &&
		    // store hours are a type of recurring date too
		    !(di->m_flags & DF_STORE_HOURS) &&
		    //!(di->m_flags & DF_KITCHEN_HOURS) &&
		    // if its a DOW in a title... "Friday Night Poker"
		    // "Tuesday Night Milonga" ... "Thursday Ladies Night" ...
		    // "Manic Monday" "Taco Tuesday" 
		    // PROBLEMS: "Joe Friday" or "Friday Fred" ...
		    //!(di->m_suppFlags & SF_DOW_IN_TITLE) &&
		    // and we are not recurring
		    !(di->m_suppFlags & SF_RECURRING_DOW) )
			// forget it
			continue;
		     
		// fix "SUN [[]] 4 [[]] 7:30pm"
		if ( di->m_hasType == (DT_TOD|DT_DAYNUM|DT_DOW|DT_TELESCOPE))
			continue;

		// stop "7:30PM - 11:30PM [[]] 4 [[]] Every Sunday"
		// for INJECTION of reateclubs.com search result in xml
		if ( di->m_hasType == (DT_TOD|DT_RANGE_TOD|
				       DT_DAYNUM|DT_DOW|DT_TELESCOPE))
			continue;
		

		// skip "7 p.m. & 8:45 p.m [[]] Friday" because it is not
		// recurring format for http://www.centerstagechicago.com/
		// theatre/shows/6612.html
		// also apply it to not telescopes and to tod ranges and
		// individual tods.
		datetype_t xd = di->m_hasType;
		xd &= ~DT_LIST_TOD;
		//xd &= ~DT_RANGE_TOD; no! this is usually store hours!
		xd &= ~DT_TELESCOPE;
		xd &= ~DT_COMPOUND;
		if ( xd == (DT_TOD|DT_DOW) &&
		     ! (di->m_suppFlags & SF_RECURRING_DOW) )
			continue;

		// . if in a calendar with no daynum!
		// . fixes weekend warriors for mercury.intouch-usa.com
		if ( di->m_calendarSection && !(di->m_hasType & DT_DAYNUM) )
			continue;
		     
		// . BUT it can't just be a plain TOD by itself
		// . fixes 
		// www.when.com/albuquerque-nm/venues/show/1061223-guild-cinema
		// . but this then breaks a couple tod entry only guys
		//   in the unm.edu url!
		// . i think its better to break a couple than assume that
		//   the isolated tod means every day! better safe than sorry
		if ( di->m_type == DT_TOD ) continue;
		// . or a simple tod range by itself is no good either
		// . this broke wholefoodsmarket.com so i had to add
		//   "seven days a week" as an HD_EVERY_DAY DT_HOLIDAY
		// . crap a lot of times for unm.edu page just have hours
		//   so assume every day!
		// . double crap because texas drums then will have the
		//   wrong dates, so let's require a DOW or something more to
		//   be on the safe side!!
		if ( di->m_hasType == (DT_TOD|DT_RANGE_TOD) ) continue;
		// if it is 6-10 with no am/pm, ignore it
		// fixes www.barkerrealtysantafe.com/listings/mls_902184/
		// TODO: set SF_AMBIG_AMPM flag to replace all these checks
		// or maybe SF_VALID_AMPM
		if ( (di->m_hasType & DT_RANGE_TOD) && 
		     ! (di->m_suppFlags & SF_NIGHT) &&
		     ! (di->m_suppFlags & SF_AFTERNOON) &&
		     ! (di->m_suppFlags & SF_MORNING) &&
		     ! (di->m_suppFlags & SF_HAD_AMPM ) &&
		     ! (di->m_suppFlags & SF_PM_BY_LIST ) &&
		     ! (di->m_suppFlags & SF_MILITARY_TIME ) &&
		     ! (di->m_suppFlags & SF_IMPLIED_AMPM ) )
		   // this was hurting "5-10:00 [[]] Tuesdays"
		   //! ( di->m_suppFlags & SF_HAD_MINUTE ) )
			continue;

		// stop "before 11pm [[]] Apr ..."
		// for www.first-avenue.com/event/2011/04/ritmo-caliente-0
		if ( ! (di->m_flags & (DF_AFTER_TOD|DF_EXACT_TOD)) )
			continue;

		// if it is just a tod list and that's it, ignore it too!
		// fixes abqcsl.org
		if ( di->m_type == DT_LIST_TOD ) continue;
		// ongoing compound dates that have a tod and no range
		// and a daynum are generally ambiguous, like
		// "Ongoing through Saturday, January 2, 2010, 5pm"
		// from 
		//www.trumba.com/calendars/albuquerque-area-events-calendar.rss
		if ( (di->m_flags & DF_ONGOING) &&
		     (di->m_hasType & DT_TOD) &&
		     !(di->m_hasType & DT_RANGE_TOD) &&
		     (di->m_hasType & DT_DAYNUM))
			continue;
		// get its flags
		dateflags_t flags = di->m_flags;
		// must be in body
		if ( ! ( flags & DF_FROM_BODY ) ) continue;
		// skip if fuzzy! that means possibly ambiguous
		// like "1/3 pound patty" does not mean "Jan 3."
		if ( flags & DF_FUZZY ) continue;
		// ignore close dates of course, when the venue is closed
		if ( flags & DF_CLOSE_DATE ) continue;
		// if no daynum and its a "funeral date", not allowed
		// to be recurring
		//if ( (flags & DF_FUNERAL_DATE) &&
		//     !(di->m_hasType & DT_DAYNUM) )
		//	continue;
		// . date must be a clear am or pm
		// . fixes guildcinema where movie times are not clear
		if (   (di->m_hasType & DT_TOD) &&
		     ! (di->m_hasType & DT_RANGE_TOD) &&
		     ! (di->m_suppFlags & SF_NIGHT) &&
		     ! (di->m_suppFlags & SF_AFTERNOON) &&
		     ! (di->m_suppFlags & SF_MORNING) &&
		     ! (di->m_suppFlags & SF_MILITARY_TIME ) &&
		     ! ( di->m_suppFlags & SF_HAD_AMPM ) )
			continue;
		// must not be a bad recurring dow
		//if ( flags & DF_BAD_RECURRING_DOW ) continue;
		// is also part of verified/inlined address?
		if ( m_bits[di->m_a] & D_IS_IN_ADDRESS ) continue;
		if ( m_bits[di->m_a] & D_IS_IN_VERIFIED_ADDRESS_NAME) continue;
		// . skip if date is also on root page! it is like store
		//   hours repeated on EVERY page in the website! so unless
		//   it has any additional dates telescoped to it, we want to
		//   ignore it if not on the root page
		// . require it be store hours because trulia.com just
		//   re-lists the open house event 
		//   "Saturday Oct 3rd, 12pm to 3pm" on the root page
		if ( (di->m_flags & DF_STORE_HOURS) &&
		     (di->m_flags & DF_ONOTHERPAGE) ) 
			continue;
		// nah, even if not store hours, we are probably getting
		// an event from a little subwindow box and want to make
		// sure that it does not capture the description in the
		// main content box... this was causing msichicago.org to
		// get the body cycle event on March 18th but it was using
		// the description of the bronzeville blues club.
		//if ( di->m_flags & DF_ONOTHERPAGE )
		//	continue;
		// fix santafeplayhouse.org
		// 7:00 PM [[]] December 10, 2009 - January 3, 2010
		// they should have "daily" or "every day at 7pm"
		if ( di->m_numPtrs == 2 ) {
			datetype_t dt0 = di->m_ptrs[0]->m_hasType;
			datetype_t dt1 = di->m_ptrs[1]->m_hasType;
			dt1 &= ~DT_YEAR;
			if ( dt0 == DT_TOD &&
			     dt1 == ( DT_DAYNUM   |
				      DT_MONTH    |
				      DT_RANGE    |
				      DT_COMPOUND ) )
				continue;
		}
		// we need at least a guess at the year range...
		if ( di->m_flags & DF_YEAR_UNKNOWN )
			continue;
		// note that the date is an event candidate so when we
		// try to supplement the event descriptions at the end we
		// avoid consider sections that contain such candidate dates
		di->m_flags |= DF_EVENT_CANDIDATE;
		// store it
		dsorted[nds++] = di;
	}
	// i guess use this now for gbqsort's dateseccmp() function
	g_sections2 = sections;
	// now sort the array by the Date's Section::m_a
	gbqsort ( dsorted , nds , 4 , dateseccmp , m_niceness );
	
	/////////////////////////////////////////////
	//
	// Set m_events[]/m_numEvents array
	//
	// . only consider date's sections that have TODs for events
	// . must also telescope to single good address to be an event
	//
	/////////////////////////////////////////////
	// for sanity check
	Section *lastsi = NULL;
	// scan the sorted dates, sorted by their Section::m_a
	for ( long i = 0 ; i < nds ; i++ ) {
		// give up control
		QUICKPOLL(m_niceness);
		// get it
		Date *di = dsorted[i];
		// . skip dates that have the word "holidays" in them because
		//   it is unclear exactly what holidays are being meant
		// . fixes events.sfgate.com solstice seed swap url which had
		//   "Holiday Hours [[]] 10:00am" but really was referring
		//   to the date 12/24 which is not really a holiday...
		//if ( di->m_suppFlags & SF_HOLIDAY_WORD ) continue;
		if ( di->m_hasType & DT_ALL_HOLIDAYS ) continue;
		// must be compound i guess
		//if ( di->m_numPtrs <= 0 ) continue;
		// reset this
		//long da = -1;
		// . get section of that date
		// . use the section of the TOD portion of the date in case
		//   the date is a telescoped date as in 
		//   www.newmexico.org/calendar/events/index.php?lID=1781
		//for ( long i = 0 ; i < di->m_numPtrs ; i++ ) {
		//	// now check for tod
		//	if ( ! ( di->m_ptrs[i]->m_hasType & DT_TOD)) continue;
		//	da = di->m_ptrs[i]->m_a;
		//	break;
		//}
		// sanity check
		//if ( da == -1 ) { char *xx=NULL;*xx=0; }
		// record that special
		//ev->m_todSection = sp [ da ];
		// get section of that address
		Section *si = sp [ di->m_a ];
		// pick the "a" of the most unique date ptr
		if ( di->m_mostUniqueDatePtr )
			si = sp [ di->m_mostUniqueDatePtr->m_a ];
		// save it
		Section *saved = si;
		// sanity check
		if ( lastsi && lastsi->m_a > si->m_a ) { char *xx=NULL;*xx=0;}
		// set it
		lastsi = si;
		// we must not be in <script> tags etc. OR in a menu!
		if ( si->m_flags & badFlags ) continue;
		// must not be open-ended
		if ( si->m_b < 0 ) { char *xx=NULL; *xx=0; }
		// reset si
		si = saved;
		// short cut
		Event *ev = &m_events[m_numEvents];
		// clear it
		memset ( ev , 0 , sizeof(Event) );
		// reference it
		ev->m_section = si;
		// set this
		ev->m_date = di;
		// tag it as an event section
		//si->m_flags |= SEC_EVENT;
		// save event since it has a decent chance of being valid
		m_numEvents++;
		// do not breach. 1000?
		if ( m_numEvents >= MAX_EVENTS ) break;
	}

	// free up - this was temporary i guess
	if ( (char *)dsorted != dbuf ) 
		mfree (dsorted,dneed,"datesort");

	// bail now if no possible events
	if ( m_numEvents <= 0 ) return true;

	/*
	// check for events turked out
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get addressdate hash
		uint32_t ah = ev->m_address->m_addrHash;
		uint32_t dh = ev->m_date->m_dateHash;
		// combine
		uint32_t addressDateContentHash = hash32h ( ah , dh );
		// check the turked table
		if ( turkVotingTable.isInTable ( &addressDateContentHash ) )
			ev->m_flags |= EV_TURK_REJECTED;
		// same for tag hashes
		uint32_t ah2 = ev->m_address->m_addrTagHash;
		uint32_t dh2 = ev->m_date->m_dateTagHash;
		// combine
		uint32_t addressDateTagHash = hash32h ( ah2 , dh2 );
		// check the turked table
		if ( turkVotingTable.isInTable ( &addressDateTagHash ) ) 
			ev->m_flags |= EV_TURK_REJECTED;
	}
	*/

	// this maps a section ptr to if it contains registration info or not.
	// NOW it includes parking info too.
	//HashTableX *rt = getRegistrationTable();
	// return false with g_errno set on error
	//if ( ! rt ) return false;
	//if ( ! setRegistrationBits() ) return false;


	/*
	  this was hurting too many pages.
	  they would have "buy tickets" in the first column of the table
	  row like in events.mapchannels.com/Index.aspx?venue=628 as
	  part of a menu. that hurt.
	  2. http://sfmusictech.com/ failed too, but seems like maybe a bug
	    for that guy
	  3. http://www.socialmediabeach.com/ possible bug too!
	  4. ...
	*/
	///////////////////////////////////
	//
	// set EV_REGISTER_DATE if the event time is associated with
	// registration or parking
	//
	///////////////////////////////////
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get the date
		Date *di = ev->m_date;
		// skip if none
		if ( ! di ) continue;
		// panic if no ptrs
		if ( di->m_numPtrs == 0 ) { char *xx=NULL;*xx=0; }
		// scan all dates in telescope
		for ( long j = 0 ; j < di->m_numPtrs ; j++ ) {
			// get it
			Date *dp = di->m_ptrs[j];
			// get date's section. section containing first date in
			// the case of telescoped dates
			Section *sn = dp->m_section;
			// telescope all the way up to see if part of a 
			// registration or parking section
			for ( ; sn ; sn = sn->m_parent ) {
				// . check it out
				// . rt table stops telescoping its keyword 
				//   when it realizes it's key section is not 
				//   the topmost  text section.
				//if ( ! rt->isInTable ( &sn ) ) continue;
				if ( !(sn->m_flags & SEC_HAS_REGISTRATION)) 
					continue;
				// set it
				ev->m_flags |= EV_REGISTRATION;
				// stop
				break;
			}
		}
	}


	///////////////////////////////////
	//
	// get the venue default address from the tagdb rec
	//
	///////////////////////////////////
	Address *venueDefault = NULL;
	Tag     *tag   = NULL;
	Address *tmpa = &m_addresses->m_venueDefault;
	//Place    places[10];
	//long     np = 0;
	// ge tnumber of "venue" tags in our site's tagdb rec
	long nv = gr->getNumTagTypes("venueaddress");
	// must only be one!
	if ( nv == 1 ) tag = gr->getTag("venueaddress");
	// . return true with g_errno set on error
	// . we have to store these places into m_places now since we need
	//   this address to be accesible when hashing the event
	if ( tag && 
	     ! setFromStr(tmpa,
			  tag->getTagData(),
			  0,
			  &m_addresses->m_pm,//places,
			  //&m_addresses->m_np,
			  //MAX_PLACES,
			  m_niceness))
		return true;
	// set "venue" to our venue default address
	if ( tag ) venueDefault = tmpa;


	// this maps a section ptr to the places it contains
	//HashTableX *at = m_addresses->getPlaceTable ();
	// this maps a section ptr to the dates it contains
	HashTableX *tt = m_dates->getTODTable ();

	// to call Dates::isHeader2() we need to create some tables
	//HashTableX *pt = m_dates->getPhoneTable   ();
	//HashTableX *et = m_dates->getEmailTable   ();
	//HashTableX *at = m_addresses->getPlaceTable ();
	//HashTableX *tt = m_dates->getTODTable ();

	///////////////////////////////////
	//
	// Set Event::m_address
	// Set EV_MULT_LOCATIONS
	// Set EV_NO_LOCATION
	// Set EV_UNVERIFIED_LOCATION
	//
	///////////////////////////////////
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// get section
		Section *sk = ev->m_section;
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;

		//long numPlaces = 0;
		long uniqueCount    = 0;
		Place *uniqueHeader = NULL;
		bool multiplePlaces = false;
		Place *sr;
		long ng ;
		Place   *goodStreet [MAX_GOOD];
		Section *goodOrig   [MAX_GOOD];
		bool     goodOwn    [MAX_GOOD];
		long     notOwned =0;
		Section **sp = m_sections->m_sectionPtrs;
		// save the section
		Section *saved = sk;
		long np = m_addresses->m_numSorted;

		// telescope up until we hit a section with addresses in it
		for ( ; sk ; sk = sk->m_parent ) {
			// mix key
			//long key = hash32h((long)sk,456789);
			// get slots for this section from address table
			//long slot = at->getSlot ( &key );
			long pi = sk->m_firstPlaceNum;
			// reset this
			HashTableX ut;
			char utbuf[1200];
			ut.set(4,4,128,utbuf,1200,false,m_niceness,"evut");

			// reset this too, to dedup address ptr because
			// an Address often has an alias or place in it that
			// is also in the same section, so do not think it
			// is another place! this should fix trumba.com rss
			// page from mistaking "unm continuing education" as
			// a separate place in the same section.
			HashTableX dat;
			char datbuf[1200];
			dat.set(4,0,128,datbuf,1200,false,m_niceness,"evdat");

			// reset these
			ng = 0;
			// loop over all address ptrs OWNED by this section.
			//for ( ; slot>=0 ; slot=at->getNextSlot(slot,&key)) {
			for ( ; pi >= 0 && pi < np; pi++ ) {
				// get the address in this slot
				//sr = *(Place **)at->getValueFromSlot(slot);
				sr = m_addresses->m_sorted[pi];
				// stop if breach - outside section
				if ( sr->m_a >= sk->m_b ) break;
				// sanity check
				if ( sr->m_a < 0 ) { char *xx=NULL;*xx=0; }

				// ignore registration addresses
				//if ( sr->m_flags2 & PLF2_TICKET_PLACE ) 
				//	continue;

				// ignore po box addresses to fix
				// corralesbosquegallery.com
				if ( sr->m_flags2 & PLF2_IS_POBOX )
					continue;

				Section *src    = sp[sr->m_a];
				//Section *origin = sp[sr->m_a];

				// ignore parking locations, but only if
				// they have no known physical address. because
				// villr.com's market event is actually in
				// a parking lot and they give the street
				// address for it... so we DO need to use that
				// as the event address. this is here to fix
				// signmeup.com which says "parking is 
				// available *at* the zoo" ... and that was
				// causing us to lose our event times because
				// they would telescope to "at the zoo" first
				// and since we didn't know that address we
				// would give up with EV_UNKNOWN_LOCATION
				if ( ! sr->m_address &&
				     ! sr->m_alias   &&
				     (sr->m_flags2 & PLF2_AFTER_AT) &&
				     (src->m_flags & SEC_HAS_PARKING) )
					continue;

				// default "max" to "sk"
				Section *max = sk;
				// . scan
				// . i added this to fix villr.com whose
				//   isoalted monthday dates which were able
				//   to telescope up to the dates above them
				//   could also use the address in that same
				//   section
				// . although why not just base our date
				//   section from the tod?????
				for ( long z=0;z<ev->m_date->m_numPtrs; z++ ) {
					// get our event date's ptr
					Date *dz = ev->m_date->m_ptrs[z];
					// "bb" is smallest section containing
					// the address "sr"
					Section *bb = sp[sr->m_a];
					// reset this
					Section *last = NULL;
					// blow up "bb" until it contains
					// the date "dz"
					for ( ; bb ; bb = bb->m_parent ) {
						// breathe
						QUICKPOLL(m_niceness);
						// stop if contains date
						if ( bb->contains2(dz->m_a))
							break;
						// record last not containd
						last = bb;
					}
					// skip if bb smallest section 
					// containing address also contained 
					// this date
					if ( ! last ) {
						// we own it no matter what!
						max = sk;//NULL;
						// stop
						break;
					}
					// otherwise, set "max" if we should
					if ( ! max || max->contains ( last ) )
						max = last;
				}

				// no no, panjea.org has two addresses
				// that have different disjoint section
				// tag hashes but they actually "come from"
				// the same tag hash if you telescope them
				// out. so the last leg of their telescope
				// is from the same section so they are
				// really "brothers" in that sense and neither
				// should be labelled as unique.
				for ( ; src ; src = src->m_parent ) {

					// if it hits a section containing
					// registration/ticket info before
					// it hits the section containing
					// our date/tod, then it should be
					// associated with registration/ticket
					// info and not this tod!! fixes
					// www.breadnm.org/custom.html because
					// it has a registration section with
					// its own address kinda embedded in
					// the whole event section AND the
					// registration keyphrases are in
					// a little span header, not in the
					// same sentence as the registration
					// address! so the sentence logic
					// in Address.cpp doesn't work on this.
					// i also added "parking" info to this
					// "rt" table too! -- but i took
					// "parking" out to fix villr.com
					//if ( rt->isInTable(&src) ) {
					if(src->m_flags&SEC_HAS_REGISTRATION){
						src = NULL;
						break;
					}

					// stop if match
					if ( src == sk ) break;
					// record where it came from
					//origin = src;
				}

				// ignore address if for tickets/registration
				if ( ! src ) continue;

				// now re-assign origin to "max"
				Section *origin = max;

				//
				// does this address belong to other dates?
				// if so, ignore it for our purposes...
				//
				long ds = tt->getSlot( &origin );
				// only do this algo if not our section
				if ( origin == sk ) ds = -1;
				// do we own this address?
				bool own = true;
				// loop over all dates in the "origin" section
				for (;ds>=0;ds=tt->getNextSlot(ds,&origin)) {
					// get date in section "sk"
					Date *dd;
					dd=*(Date **)tt->getValueFromSlot(ds);
					// skip if our date, or a brother date
					if ( dd->m_hardSection ==
					     ev->m_date->m_hardSection )
						continue;
					// blackbirdbuvette has 
					// (between 5th & 6th) referrig to
					// an address but we pick it up as
					// a date, so ignore those! they cannot
					// claim ownership of this address
					if (dd->m_hasType==
					    (DT_DAYNUM|DT_LIST_DAYNUM))
						continue;
					////////////////////
					//
					// STORE HOURS algorithm
					//
					// if address is owned by a bunch of
					// "store hours" dates, then we can
					// use the address from there
					//
					///////////////////////

					if ( dd->m_flags & DF_STORE_HOURS )
						continue;
					if ( dd->m_flags & DF_KITCHEN_HOURS )
						continue;

					// ignore ticket dates
					if ( dd->m_flags & DF_NONEVENT_DATE )
						continue;
					if ( dd->m_flags5 & DF5_IGNORE ) 
						continue;

					// ignore pub dates
					if ( dd->m_flags & DF_PUB_DATE )
						continue;

					// ignore copyright dates
					if ( dd->m_flags & DF_COPYRIGHT )
						continue;

					// and dates that have header as base
					//if ( dd->m_flags & DF_UPSIDEDOWN )
					//	continue;

					// for events.sfgate.com/san-francisco-
					// ca/venues/show/6136-exploratorium
					// it has some old years mentioned
					// in this venues description that
					// were claiming the address as their
					// own, so stop that!
					// ACTUALLY, let's try just only
					// allowing tods to claim an address!
					if ( ! (dd->m_hasType & DT_TOD) )
						continue;

					// "before 11pm" TODs don't cut it,
					// we need "after 11pm" or "at 11pm"
					if ( !(dd->m_flags&
					       (DF_AFTER_TOD|DF_EXACT_TOD)))
						continue;

					//if ( dd->m_hasType==
					//   (DT_YEAR|DT_MONTH|DT_COMPOUND) &&
					//   dd->m_year < m_year0 )
					//	continue;

					// if this tod is part of our date
					// then we need to skip it!
					// no, could be part of multiple
					// telescopes!
					//long zmax = ev->m_date->m_numPtrs;
					//long z;
					//for ( z = 0 ; z < zmax ; z++ ) {
					//	// get our event date's ptr
					//	Date *dz=ev->m_date->m_ptrs[z];
					//	// compare to dd
					//	if ( dz == dd ) break;
					//}
					// if part of our date is in the
					// "origin" section of address
					//if ( z < zmax ) continue;


					// . if same tag hash that is ok
					// . no that section described right
					//   below on panjea.org is a brother
					//   and we should ignore its address
					//if(dd->m_hardSection->m_tagHash ==
					//ev->m_date->m_hardSection->m_tagHash)
					//	continue;
					// break if it has a tod, we are done
					//if ( dd->m_hasType & DT_TOD ) break;
					// well for panjea.org the
					// "Albuquerque's Hardwood Art Center"
					// location in a box on its own does
					// not contain TOD dates, but has
					// others, so for now just stop on
					// any date, and later we might
					// allow this address through if
					// one of the dates it does contain
					// is in our telescope! (TODO?)
					own = false;
					break;
				}
				// if had a TOD, sr belongs to another event
				//if ( ds >= 0 ) 
				//	continue;

				// if this place is really an alias for
				// another address, just ignore it for
				// these purposes... but only if not owned by
				// us. trying to fix
				// www.reverbnation.com/venue/448772
				// which has a table of events and the first 
				// one happens to have its own location, which 
				// is really just an alias to "Low Spirits", 
				// the venue address
				if ( ! own && sr->m_alias ) continue;


				// if afterat or lat/lon address was 
				// supplanted by a definitive lat/lon address
				// as in the case of the stubhub xml feed
				// then detect that. stub hub will have
				// stuff like "at StubHub" "at blah center"
				// but we need to focus on the lat/lon!
				if ( sr->m_flags3 & PLF3_SUPPLANTED ) continue;
				if ( sr->m_flags3 & PLF3_LATLONDUP  ) continue;


				long th = origin->m_tagHash;
				// sometimes one address has multiple aliases
				// or simple places referencing it via
				// Place::m_alias or Place::m_address ,
				// where m_address means the simple place is
				// a street or name actually in the address
				// itself, so we have to check for those dups!
				Address *pa = NULL;
				if ( sr->m_address ) pa = sr->m_address;
				// allow alias to override because if we have
				// an intersection based address like
				// "14th and curtis" as in denver.org url,
				// that will have its own address, but it
				// also aliases to "1000 14th street, #15".
				// so prefer the alias in such events.
				if ( sr->m_alias   ) pa = sr->m_alias;
				bool dup = false;
				if ( pa ) {
					// use the street hash since for
					// trumba.com the place name of
					// an inlined address did a separate
					// lookup in placedb than the
					// street! and they both pointed to
					// different Address objects, BUT they
					// street hash was the same! that
					// was an issue with 
					// "Albuquerque Little Theater" since
					// that name was in placedb, and so
					// was its street!
					long sh = (long)pa->m_street->m_hash;
					// skip if dup
					if ( dat.isInTable(&sh) ) dup = true;
					// add it in
					else if(!dat.addKey(&sh)) return false;
				}
				// count it in hash table if not a dup
				if ( !dup && !ut.addTerm32(&th)) return false;
				// add to good list
				goodStreet [ng] = sr;
				goodOrig   [ng] = origin;
				goodOwn    [ng] = own;
				ng++;
				// stop?
				if ( ng < MAX_GOOD ) continue;
				log("events: hit MAX_GOOD");
				break;
				// count it
				//numPlaces++;
			}
			// telescope up if no places
			if ( ng == 0 ) continue;
			// reset this
			uniqueCount = 0;
			uniqueHeader = NULL;
			long long uniqueHash = 0LL;
			multiplePlaces = false;
			notOwned = 0;
			// loop over array
			for ( long i = 0 ; i < ng ; i++ ) {
				// grab it
				Place   *sr     = goodStreet[i];
				Section *origin = goodOrig  [i];
				bool     own    = goodOwn   [i];
				// record the tag hashes of the
				// addresses we encounter
				long th = origin->m_tagHash;
				// get count
				long count = ut.getScore32(&th);
				// if not owned by us, skip it
				if ( ! own ) {
					notOwned++;
					continue;
				}
				// if this section had multiple brothers
				// each withs its own address then ignore them.
				// they could just be brother sections. we 
				if ( count >= 2 ) {
					multiplePlaces = true;
					continue;
				}

				// get the street hash
				long long sh = sr->m_hash;
				// use street hash of alias or address if
				// we have that though
				Address *pa = NULL;
				if ( sr->m_address ) pa = sr->m_address;
				// allow alias to override because if we have
				// an intersection based address like
				// "14th and curtis" as in denver.org url,
				// that will have its own address, but it
				// also aliases to "1000 14th street, #15".
				// so prefer the alias in such events.
				if ( sr->m_alias   ) pa = sr->m_alias;
				if ( pa ) sh = pa->m_street->m_hash;

				// if we have a unique guy already and
				// we are a dup, skip this then!
				if ( uniqueCount && uniqueHash == sh )
					continue;

				// got one occurences of this tag hash
				uniqueCount++;
				// store it
				if ( ! uniqueHeader ) uniqueHeader = sr;
				// if we have another occurence of this street
				// hash and uniqueHeader is a fake street
				// and this is a real street, then replace it.
				// that way if a placedb entry changes our
				// place name for an event then it won't
				// change Event::m_addressTurkTagHash32 used
				// by the turks.
				else if(
				  (uniqueHeader->m_flags2 & PLF2_IS_NAME) &&
				  ! (sr->m_flags2 & PLF2_IS_NAME) )
					uniqueHeader = sr;
			     
				// store this too
				uniqueHash = sh;
			}
			// . ok stop it now, we had something on the hook
			// . no! we inevitably meet some addresses in the
			//   event table from other events, so we just
			//   need to ignore those!
			//if ( numPlaces != 0 ) break;
			if ( uniqueCount    ) break;
			// . if the place was "owned" by another date then
			//   we should stop...
			// . this fixes peachpundit from allowing a date in
			//   the comment section to use the address/street
			//   in the article section, which is "owned" by 
			//   another date
			// . but this breaks www.reverbnation.com/venue/448772
			//   which has a table of events and the first one
			//   happens to have its own location, which is really
			//   just an alias to "Low Spirits", the venue address
			// . haveing notOwned>0 breaks santafeplayhouse.org
			//   now because the first address we get is
			//   topologically closer to the parking lot open
			//   time at 6pm... so we do not own that address
			//   and we stop without a good address... i tried
			//   commenting this line out but it got a few more
			//   events elsewhere that had the wrong address, so
			//   err on the side of caution.
			if ( notOwned > 0 ) break;
		}

		// if nothing at all, try to use venue default
		if ( uniqueCount == 0 && 
		     notOwned    == 0 && 
		     ! multiplePlaces &&
		     venueDefault ) {
			ev->m_flags |= EV_VENUE_DEFAULT;
			ev->m_address = venueDefault;
			// no need to do anything below!
			continue;
		}

		// if nothing that is a problem too
		if ( uniqueCount == 0 ) {
			if ( multiplePlaces )
				ev->m_flags |= EV_MULT_LOCATIONS;
			else
				ev->m_flags |= EV_NO_LOCATION;
			continue;
		}

		if ( uniqueCount >= 2 ) {
			ev->m_flags |= EV_MULT_LOCATIONS;
			continue;
		}

		// . if our brothers had locations and we did not, then we
		//   probably did, but it was unrecognized by us... 
		// . better safe than sorry.
		// . this fixes santafe.org which had a sibling event section
		//   that had a street location unrecognized by us, and it 
		//   ended up telescoping up to the college address!
		// . this hurts panjea.org because one of the events has
		//   "at the harwood art center" in its section.
		// . so if we had better location identification so that
		//   santafe.org worked, we could take this constraint out,
		//   but for now we err on the side of caution
		if ( multiplePlaces ) {
			ev->m_flags |= EV_MISSING_LOCATION;
			continue;
		}

		Place *street = NULL;
		// assign the address now
		if ( uniqueCount == 1 )
			street = uniqueHeader;

		// must be non-null
		if ( ! street ) { char *xx=NULL;*xx=0; }
		// must be in the body
		if ( street->m_a < 0 ) { char *xx=NULL;*xx=0; }

		// get his section
		Section *ss = m_sections->m_sectionPtrs[street->m_a];
		// . IF we contain different phone numbers, emails or the
		//   same subfields/subheaders THEN they are not allowed to 
		//   pair up because they are talking about different things.
		// . use false,false because unm.edu has a celina gonzales
		//   phone # in the tod cell and different phone numbers in
		//   the address cell...
		// . crap, but then we screw up santafe.org which has
		//   a header address, then "events near by this" and then
		//   a table of events. we don't regcognize the address in
		//   one of the events and it pairs up the open hours with
		//   the header address even though they have different phone
		//   numbers.
		// . so let's sacrifice unm.edu i guess to fix santafe.org.
		//   better to miss some than to get some wrong. later we
		//   could fix unm.edu by noting that the phone numbers with
		//   the exception of the last 2 digits are the same!
		// . well, for now i fixed santafe.org with the 
		//   EV_MISSING_LOCATION algo above
		long ret =m_dates->isCompatible2(saved,ss,false);
		// return false with g_errno set on error
		if ( ret == -1 ) return false;
		// if not compatible, stop!
		if ( ret == 0 ) {
			ev->m_flags |= EV_NOT_COMPATIBLE;
			continue;
		}

		// get the address containing the street
		Address *addr = street->m_address;
		// use alias?
		// . this is making a valid street address reference
		//   the same address somewhere else on the page, and thereby
		//   messing us up because we need to read the original address
		//   for turking/display/announcing purposes...
		if ( ! addr && street->m_alias ) 
			addr = street->m_alias;

		// must be in an inlined or verfied address
		if ( ! addr ) {
			ev->m_flags |= EV_UNVERIFIED_LOCATION;
			continue;
		}

		if ( addr->m_flags & AF_AMBIGUOUS ) {
			ev->m_flags |= EV_AMBIGUOUS_LOCATION;
			continue;
		}
			

		// store address, if it is not compatible at least we can
		// see the address in the INvalid events table
		ev->m_address = addr;

		// save street! could be fake street
		ev->m_origPlace = street;

		// if the address is not inlined or verified THEN consider
		// it an unknown address
		bool inlined   = addr->m_flags & AF_INLINED;
		bool verified1 = addr->m_flags & AF_VERIFIED_PLACE_NAME_1;
		bool verified2 = addr->m_flags & AF_VERIFIED_PLACE_NAME_2;
		bool verified3 = addr->m_flags & AF_VERIFIED_STREET;
		bool aliased   = (bool)(street->m_alias);
		if ( ! inlined   && 
		     ! verified1 && 
		     ! verified2 && 
		     ! verified3 && 
		     ! aliased     ) {
			ev->m_flags |= EV_UNVERIFIED_LOCATION;
			continue;
		}

		// getPlaceTable() NOW does allow po boxes into there -
		// so we need this check!
		if ( addr->m_street->m_flags2 & PLF2_IS_POBOX ) {
			ev->m_flags |= EV_IS_POBOX;
			continue;
		}


		if ( addr->m_street->m_flags2 & PLF2_TICKET_PLACE ) {
			ev->m_flags |= EV_TICKET_PLACE;
			continue;
		}

		// get his smallest containing section
		//Section *as = m_sections->m_sectionPtrs[addr->m_a];

		// now if the section that contains the address is not 
		// "compatible" with the section that contains the tod,
		// we can not pair them up. basically this function
		// does not allow similar items to coexist in both sections.
		// the idea is that if they both have a phone number then
		// they are both the same "item" section and one is not
		// a "header" for another.
		//if ( ! m_dates->isHeader2 ( si, as, NULL,pt,et,NULL,at ) ) {
		//	ev->m_flags |= EV_NOT_COMPATIBLE;
		//	continue;
		//}
	}


	///////////////////////////////////
	//
	// set Event::m_closeDates[] and Event::m_numCloseDates
	//
	///////////////////////////////////
	Section *lastOwned = NULL;
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get section
		Section *si = ev->m_section;
	loop:
		// get the dates in this section from "tt"
		long slot = tt->getSlot( &si );
		// loop over all dates in the "origin" section
		for (;slot >= 0 ; slot = tt->getNextSlot(slot,&si) ) {
			// get date in section "sk"
			Date *dd = *(Date **)tt->getValueFromSlot(slot);
			// skip if our date, or a brother date
			if ( dd->m_hardSection == ev->m_date->m_hardSection )
				continue;
			// if closed date in neighboring hard section like
			// an <li> tag then, let it go
			if ( dd->m_flags & DF_CLOSE_DATE ) continue;
			// this guy will stop us
			break;
		}
		// if we owned all the dates, telescope some more, but
		// first save it!
		if ( slot < 0 && si ) { 
			// save it
			lastOwned = si;
			// telescope some more
			si = si->m_parent;
			goto loop;
		}
		// ok, grab all the closed dates in lastOwned
		slot = tt->getSlot( &lastOwned );
		// count them
		long ncd = 0;
		// loop over all dates in the "origin" section
		for (;slot >= 0 ; slot = tt->getNextSlot(slot,&lastOwned) ) {
			// get date in section "sk"
			Date *dd = *(Date **)tt->getValueFromSlot(slot);
			// skip if not closed
			if ( ! ( dd->m_flags & DF_CLOSE_DATE ) ) continue;
			// grab the telescope if there
			if ( dd->m_telescope ) continue;//dd = dd->m_telescope;
			// stop if overflow
			if ( ncd >= MAX_CLOSE_DATES ) break;
			// add it to our event
			ev->m_closeDates[ncd++] = dd;
		}
		// set this
		ev->m_numCloseDates = ncd;
	}
		



	///////////////////////////////////
	//
	// Improve Event::m_address if we can
	//
	///////////////////////////////////
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// get section
		//Section *si = ev->m_section;
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get its address
		Address *aa = ev->m_address;
		// shortcut
		long na = addresses->m_am.getNumPtrs();//na;	
		// get our adm1
		char *admaa = NULL;
		if      ( aa->m_adm1 ) admaa = aa->m_adm1->m_adm1;
		else if ( aa->m_zip ) admaa = aa->m_zip->m_adm1;
		else if ( aa->m_city && aa->m_city->m_adm1[0] )
			admaa = aa->m_city->m_adm1;
		else if ( aa->m_flags3 & AF2_LATLON );
		else { char *xx=NULL;*xx=0; }
		// before assigning this address to this event, let's see
		// if we can find the same street address on the page but
		// with a valid place name 1 or 2...
		char vflags = 0;
		vflags |= AF_VERIFIED_PLACE_NAME_1;
		vflags |= AF_VERIFIED_PLACE_NAME_2;
		for ( long k = 0 ; k < na && !(aa->m_flags&vflags); k++ ){
			// breathe
			QUICKPOLL(m_niceness);
			// get kth address
			Address *ka = (Address *)addresses->m_am.getPtr(k);
			// ka must have verified place name
			if ( ! ( ka->m_flags & vflags ) ) continue;
			// skip if no street hash match
			if(ka->m_street->m_hash!=aa->m_street->m_hash)continue;
			// street num must match too! doh!
			if(ka->m_street->m_streetNumHash !=
			   aa->m_street->m_streetNumHash)
				continue;
			// and indicators must match (ave,blvd,...,NE)
			if(ka->m_street->m_streetIndHash !=
			   aa->m_street->m_streetIndHash)
				continue;
			// and same adm1, city hash
			//if(ka->m_cityHash != aa->m_cityHash) continue;
			if ( ka->m_cityId32 != aa->m_cityId32) continue;
			// get the two adm1 codes
			char *adm1 = NULL;
			if      ( ka->m_adm1 ) adm1 = ka->m_adm1->m_adm1;
			else if ( ka->m_zip  ) adm1 = ka->m_zip->m_adm1;
			else { char *xx=NULL;*xx=0; }

			// compare now
			if ( admaa[0] != adm1[0] ) continue;
			if ( admaa[1] != adm1[1] ) continue;

			//(ka->m_city.m_adm1[0]!=aa->m_city.m_adm1[0])continue;
			//(ka->m_city.m_adm1[1]!=aa->m_city.m_adm1[1])continue;
			
			// ok, use it instead!
			aa = ka;
			break;
		}
		// reassign
		ev->m_address = aa;
	}

	///////////////////////////////////
	//
	// Set Event::m_titleStart/m_titleEnd
	//
	///////////////////////////////////
	/*
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// get section
		Section *si = ev->m_section;
		// skip if already bad
		//if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// assume none
		ev->m_titleStart = -1;
		ev->m_titleEnd   = -1;
		// scan each section for text as we telescope up
		for ( ; si ; si = si->m_parent ) {
			// skip if sentence section
			if ( si->m_flags & SEC_SENTENCE ) continue;
			// . skip if no text
			// . sometimes it contains a subsection with text!
			//   so take this out
			//if ( si->m_flags & SEC_NOTEXT ) continue;
			// scan its words
			long a = si->m_a;
			long b = si->m_b;
			// allow a full scan
			//long b = m_nw;
			// do not bleed into next event though
			//if ( next ) b = next->m_a;
			// start at first word in section
			long j = a;
			// scan for a good title start
			for ( ; j < b ; j++ ) {
				// breathe
				QUICKPOLL(m_niceness);
				// skip if punct or tag, because the title 
				// must start on an alnum word
				if ( ! wids[j] ) continue;
				// get section
				Section *sj = sp[j];
				// is word in a menu section? skip that junk
				if ( sj->m_flags & badFlags ) continue;
				// skip if in a date
				if ( m_bits[j] & D_IS_IN_DATE    ) continue;
				// . skip if menu
				// . no, event menus have this set for
				//   their titles?
				// . messed up the www.zvents.com/albuquerque-
				//   nm/events/show/88688960-sea-the-invalid-
				//   mariner
				// . try again with new SEC_MENU algo
				if ( sj->m_flags & SEC_MENU ) continue;
				if ( sj->m_flags & SEC_MENU_HEADER ) continue;
				if ( sj->m_flags & SEC_INPUT_HEADER ) continue;
				if ( sj->m_flags & SEC_INPUT_FOOTER ) continue;
				// that is good!
				break;
			}
			// if no, telescope up some
			if ( j >= b ) continue;
			// sanity check
			if ( ! wids[j] ) { char *xx=NULL;*xx=0; }
			// save it
			long startj = j;
			// max it
			long max = b; if ( max > j + 20 ) max = j + 20;
			// count alnums
			long alnums = 0;
			// find end of title
			for ( ; j < max ; j++ ) {
				// stop at first tag
				if ( tids[j] ) break;
				// or if hit a date!
				if ( m_bits[j] & D_IS_IN_DATE    ) break;
				// count alnums
				if ( wids[j] ) alnums++;
				//if ( m_bits[j] & D_IS_IN_ADDRESS ) break;
				// . or \n too
				// . no "Ongoing Classes in African\nDance"
				//   was messing up on panjea.org page
				//if ( ww->hasChar(j,'\n') ) break;
			}
			// remove punct words at end
			for ( ; ! wids[j-1] ; j-- );

			// set start of it
			ev->m_titleStart = startj;
			// does not include word #j
			ev->m_titleEnd = j;
			// all done
			break;
		}
	}
	*/


	/////////////////////////////////
	//
	// Set EV_NO_YEAR flags
	//
	// . if event has a daynum without attached year, set EV_NO_YEAR
	// . when.com/albuquerque-nm/venues/show/1061223-guild-cinema
	//   for that url, we have movie times like 2/20[[]]10pm but
	//   they have no year! so if we have a daynum, then we must 
	//   have a year now too! 
	// . we could have just not allowed this to be an event, but sometimes
	//   its better to see why it did not make it as an event...
	/////////////////////////////////
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// get our date, the one and only tod based date
		Date *di = ev->m_date;
		// if it has no year, set this
		if ( ! ( di->m_hasType & DT_DAYNUM ) ) continue;
		if (   ( di->m_hasType & DT_YEAR   ) ) continue;
		// we use Date::m_minPubDate + 1 year as the interval if
		// DF_ASSUMED_YEAR is set
		if ( di->m_flags & DF_ASSUMED_YEAR ) continue;
		
		// fix for tinkertown:
		// "daily 9 - 6 [[]] Apr 1 - Nov 1" for
		// http://www.collectorsguide.com/ab/abmud.html
		if ( di->m_hasType & DT_RANGE_MONTHDAY )
			continue;
		// get section
		//Section *si = ev->m_section;
		// we had a daynum with no year attached to it!!
		ev->m_flags |= EV_NO_YEAR;
	}

	// . was this url injected (a fake url) from the addevent page?
	// . in this case we know there is no clock on the page so we can
	//   avoid setting EV_SAMEDAY and people can add their events without
	//   that fear.
	bool isAddEventUrl = false;
	if ( strncmp(m_url->m_url,"http://www.flurbit.com/events/",30)==0 )
		isAddEventUrl = true;
	if ( strncmp(m_url->m_url,"http://www.eventguru.com/events/",32)==0 )
		isAddEventUrl = true;
	if ( strncmp(m_url->m_url,"http://www.eventwidget.com/events/",34)==0 )
		isAddEventUrl = true;

	m_isStubHubValid = true;
	m_isStubHub      = false;
	m_isEventBrite   = false;

	bool isEventFeed = false;

	if ( strncmp(m_url->m_url,"http://www.stubhub.com/",23)==0) {
		isEventFeed = true;
		m_isStubHub = true;
	}

	if ( strncmp(m_url->m_url,"http://www.facebook.com/",24)==0)
		isEventFeed = true;

	if ( strncmp(m_url->m_url,"http://www.eventbrite.com/",26)==0) {
		isEventFeed    = true;
		m_isEventBrite = true;
	}

	////////////////////////////
	//
	// Set Event::m_maxStartTime, which we use below
	//
	////////////////////////////
	struct tm *ts = gmtime ( &m_spideredTime );
	// get year range we want to hash start dates for
	m_year0 = ts->tm_year + 1900;
	// right endpoint is open ended, so add 2 instead of 1
	m_year1 = m_year0 + 2;
	// test
	//if ( ! strcmp ( m_xd->m_coll, "test" ) ) 
	//	m_year1 += 5;
	//m_year1 += 2;
	// reset this. this holds all the intervals for this event
	m_sb.reset();
	// . set Event::m_maxStartTime
	// . and set the time intervals for this event
	// . Event::m_intervalsOff will be an offset into the array of 
	//   Intervals stored in m_sb.
	// . Event::m_ni is the number of intervals we stored for this event
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// get section
		//Section *sn = ev->m_section;
		// assume no good
		ev->m_eventId = -1;
		// skip if bad now
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// set its Event::m_intervalsOffset and Event::m_ni
		// for the list of intervals over which the event occurs.
		// these intervals are of the form [a,b) and use the Interval
		// class.
		if ( ! getIntervals ( ev , &m_sb ) ) return false;
		// if intersection of the times was 0....
		// . crap! now a section may contain multiple tods, i.e.
		//   events, and some may be valid times and some may not
		//   be, so simply setting flags in the section is not
		//   a good idea!!!
		// . ALSO, if timezone is not found in the lookup, like if
		//   we do not have the city/state in our table, then
		//   this will be zero as well!!!
		if ( ev->m_maxStartTime == 0 ) ev->m_flags |= EV_EMPTY_TIMES;

		// let's intersect our time intervals with the first 
		// interval  we encounter while telescoping that has our
		// same address!
		// often times multiple events are described under the same
		// location. this can be because we did not detect a second
		// location or it is describing an event within a store hours.
		//
		// examples from http://www.collectorsguide.com/ab/abmud.html :
		//
		// "Tue-Sun 9-5. Closed Mon and holidays. ...
		//  Every Sunday before 1pm and all day the first Wednesday 
		//  of each month there is no charge for general admission... "
		//
		// "DynaTheater showing films on the hour beginning at 10am 
		//  daily. ... Daily 9-5, closed Thanksgiving & Christmas and "
		//  non-holiday Mondays in Jan. & Sept."
		//
		// "Gates open daily 8-5; Visitors' Center open daily 10-5pm. 
		//  Closed Thanksgiving, Christmas and New Year's Day. "
		//

		// always telescope to the store hours? hasType DOW/RANGE_TOD.
		// telescope to the nearest store hours date that intersects
		// with you... (non empty intersection) unless you are
		// store hours format yourself...

		// print exceptino/closed dates in red... those should always
		// telescope with store hours... or the first recurring header
		// they have when telescoping. after finding all dates
		// just find those following a "Closed" or "except" and
		// set their DF_CLOSED flag... and each store hours date
		// should telescope to all such closed dates...


		//if ( ev->m_maxStartTime == 0 ) {
		//	ev->m_flags |= EV_EMPTY_TIMES;
		//	continue;
		//}
		// . eliminate events in the past
		// . http://eventful.com/albuquerque/venues/the-filling-stat
		//   ion-/V0-001-001121221-1 is a venue page and 
		//   was getting a single comment in the past, even though it
		//   had a table of multiple events in the future. but the 
		//   multiple events in the future each had a link and their
		//   own page. 
		// . the "event" we do pick up is a little section describing
		//   the venue that contains a date when the informatino was
		//   added. if we can record the section as often having 
		//   dates in the past we might decide it is not a good
		//   event section?
		// . let's let this slide so that text in these sections does
		//   not telescope out add get indexed under future event
		//   descriptions (burtstikilounge had some events in the
		//   past and some in the future)
		// . "now" is in UTC but ev->m_maxStartTime is relative
		//   to the event's timezone, therefore, add 24 hrs to now
		// . this is so we can avoid comments!
		// . hawaii is 10 hours off gmt, so reduced what we subtracted
		//   from 24 hrs to 10 hrs
		else if ( ev->m_maxStartTime < now - 10*3600 )
			ev->m_flags |= EV_OLD_EVENT;
		// . if same day, do not allow it, too often it is a clock
		// . assume clock might be ahead of our clock up to 3 hrs!
		// . TODO: make exception if manually added! (mdw)
		else if ( (ev->m_maxStartTime < now + 3 * 3600) &&
			  // if it was added from our "add event" page
			  // PageAddEvent.cpp then allow it to pass this.
			  // people like to add events that are occuring
			  // within 3 hrs from now!!
			  ! isAddEventUrl &&
			  // stubhub xml feed has no clock in it! i did
			  // this mostly because the event i was using
			  // for testing was same day...
			  ! isEventFeed )
			ev->m_flags |= EV_SAMEDAY;
		// skip if no intervals -- empty times
		if ( ev->m_ni <= 0 ) continue;
		// get intervals ptrs
		Interval *int3 = (Interval *)(ev->m_intervalsOff + 
					      m_sb.getBufStart());
		// get first interval duration
		long duration = int3[0].m_b - int3[0].m_a;
		// is first interval > 24hrs long? that is not good
		// and we want to exclude from the gbresultset:1 and 2
		if ( duration > 24*3600 ) 
			ev->m_flags |= EV_LONGDURATION;
	}


	//
	// was here
	//


	// not if rss file extension
	bool isRSSExt = false;
	char *ext = m_url->getExtension();
	if ( ext && strcasecmp(ext,"rss") == 0 ) isRSSExt = true;
	if ( m_xd->m_contentTypeValid &&
	     m_xd->m_contentType == CT_XML ) isRSSExt = true;

	////////////////
	//
	// add SEC_TOD_EVENT flags
	//
	// . we identify max tod sections and make it so brothers in a list of
	//   two or more such sections cannot telescope to each other's dates,
	//   and so we do not share each other's event descriptions. fixes 
	//   abqtango.com and salsapower.com and abqfolkfest.org from grabbing
	//   event description text from "failed" event sections that are 
	//   brothers to successful event sections.
	// . basically we assume all brother sections, where two or more
	//   brothers contain tods (times of day), are each a separate event
	//   and set set SEC_TOD_EVENT flag on all the brothers, and we do
	//   not share event description between them. 
	// . IT IS BETTER TO MISS a good description than index the wrong one!
	// . now we only add this when all non-header sections that contain
	//   text have the same tagid, and its <p> or <tr>... similar to
	//   our algorithm for set SEC_HEADING_CONTAINER in Sections.cpp,
	//   which requires that of the brothers as well, otherwise it nixes
	//   the SEC_HEADING_CONTAINER bits
	//
	////////////////
	/*

	long imax = m_dates->m_numDatePtrs;
	// don't do this algo if you are trumba.com or rss feed because
	// we use it when events are not well delineated. and since trumba
	// repeats the tod twice, all the xml tags in each event were getting
	// SEC_TOD_EVENT set and we lost the title, etc. of each event
	if ( isRSSExt ) imax = 0;

	Date *last = NULL;
	// now scan the TOD dates and blow up their sections as big as we
	// can such that they do not contain the prev TOD in the list.
	// and set prev TOD's section smaller if he would hit you.
	// now scan the dates we found and set the bits for the words involved
	for ( long i = 0 ; i < imax ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not tod
		if ( ! ( di->m_hasType & DT_TOD ) ) continue;
		// skip if fuzzy??? try to fix "noon day ministry" for unm.edu
		if ( di->m_flags & DF_FUZZY ) continue;
		// "before 11pm" isn't enough to claim the description
		//if ( ! (di->m_flags & (DF_AFTER_TOD|DF_EXACT_TOD))) continue;
		// skip if telescoper
		if ( di->m_hasType & DT_TELESCOPE ) continue;
		// skip if pub date now
		if ( di->m_flags & DF_PUB_DATE ) continue;
		// ignore registration dates to fix southgatehouse.com
		// desc of first event
		if ( di->m_flags & DF_NONEVENT_DATE ) continue;

		// what about hasregistration for events?
		// fix for signmeup.com so we do not get its registration
		// dates make us set SEC_TOD_EVENT and we lose the desc.
		if ( di->m_section->m_flags & SEC_HAS_REGISTRATION ) continue;

		// save
		Date *prev = last;
		// replace
		last = di;
		// skip if no prev
		if ( ! prev ) continue;
		// get smallest section containing this tod
		Section *si = di->m_section;
		// get prev section
		Section *ps = prev->m_section;
		// if we are in same section as prev, ignore us!
		if ( si == ps ) continue;
		// or if prev contains us, ignore us
		if ( ps->contains ( si ) ) { 
			// reset "last" back to prev in case it contains more!
			last = prev; 
			continue;
		}

		// set this
		Section *biggest = NULL;
		// loop over this
		Section *pp = si;
		// need at least one hard section
		bool isHard = false;
		// blow up until contain prev
		for ( ; pp ; pp = pp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if contains prev guy
			if ( pp->contains ( ps ) ) break;
			// need a hard section somewhere
			if ( m_sections->isHardSection(pp) ) isHard = true;
			// but we should count span tags as hard for this
			// since gwair.org uses them that way. otherwise
			// we lose an event because it gets the
			// EV_BAD_STORE_HOURS flag set on it because it
			// telescopes up to an otherwise much larger
			// SEC_TOD_EVENT section which includes a brother
			// event that should have been in its own SEC_TOD_EVENT
			// section but wasn't because we didn't recognize
			// span tags as hard tags. and EV_BAD_STORE_HOURS got
			// set because the much larger SEC_TOD_EVENT section
			// contained a non-fuzzy month or daynum, specifically
			// that of a brother event!
			if ( pp->m_tagId == TAG_SPAN ) isHard = true;
			// otherwise, set it
			biggest = pp;
		}
		// skip if contains no hard section
		if ( ! isHard ) continue;
		// record that
		di->m_maxTODSection = biggest;
		// blank out
		biggest = NULL;
		// blow up prev in the same way
		for ( pp = ps ; pp ; pp = pp->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if contains prev guy
			if ( pp->contains ( si ) ) break;
			// otherwise, set it
			biggest = pp;
		}
		// set that if smaller than what it had
		if ( prev->m_maxTODSection &&
		     prev->m_maxTODSection->contains(biggest) )
			// shrink it so it doesn't encompass "si"
			prev->m_maxTODSection = biggest;
	}
	//
	// now set SEC_TOD_EVENT bit on all sections that have maxTODSection
	// or have a brother that does
	//
	for ( long i = 0 ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not tod
		if ( ! di->m_maxTODSection ) continue;
		// get his section then
		Section *si = di->m_maxTODSection;
		// skip if list is done
		if ( si->m_used == 91 ) continue;
		// get first brother it has
		Section *b = si;
		// back up
		for ( ; b->m_prevBrother ; b = b->m_prevBrother )
			// breathe
			QUICKPOLL(m_niceness);
		// - this loop was hurting abqfolkfest.org after i turned off 
		//   addImpliedSections() because a section at the top
		//   of the list is a <ul>, so i have commented out the
		//   below loop to fix that.
		// - the results of turning it off:
		// - it hurt the turkey trot description on signmeup.com
		//   because of some paragraphs with tods. BUT it is counting
		//   the registration tods!! which are marked hasregistration
		//   so we should fix that now.
		// - this hurt villr.com - we lost that description -- BUT
		//   once we add delimeter based implied sections that should
		//   contain the tod event sections into their own place.
		// - this hurt desc in fwchamber.org since they repeated the
		//   same tod in multiple brother sections
		// - this helped many others, including removing the
		//   tingley beach description from collectorsguide.com
		// - 
		// now go forward and set SEC_EVENT and m_used to 91
		for ( ; b ; b = b->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . take out for now... mdw mdwmdw
			// . maybe just don't telescope to a brother date
			//   with the same tagId unless it has 
			//   SEC_HEADING_CONTAINER/NIXED... set (its a heading)
			//if ( goodList ) b->m_flags |= SEC_TOD_EVENT;
			b->m_flags |= SEC_TOD_EVENT;
			// do not re-do
			b->m_used = 91;
		}
	}
	*/

	// set this for printing out in the validator
	//m_revisedValid = m_numValidEvents;

	/*
	  MDW: try taking this out and fixing crap another way

	// 
	// set EV_BAD_STORE_HOURS flag if we are assuming the date is store 
	// hours but it is in an SEC_TOD_EVENT section that has another
	// month/day date in it... 
	// this fixes patpendergrass.com which has a lot of event sections
	// and each has a month/day date, but one of them we think has
	// a store hours in it beause it has "Saturday morning from 10am-noon"
	// even though it is really for "March 19, 2005"
	//
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// . skip it if it got disqualified
		// . now we are much more liberal with "banning" text
		//   from sections we thought to be events...
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get the date
		Date *ed = ev->m_date;
		// skip if not store hours
		if ( ! ( ed->m_flags & DF_STORE_HOURS ) ) continue;
		// get event's section (around the tod i think)
		Section *si = ev->m_section;
		// . blow up until hits SEC_TOD_EVENT section
		for ( ; si ; si = si->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			//if ( si->m_flags & SEC_TOD_EVENT ) break;
			// the new SEC_EVENT_BROTHER logic should work, no?
			if ( si->m_flags & SEC_EVENT_BROTHER ) break;
		}
		// skip if none
		if ( ! si ) continue;
		// get the dates in this section from "tt"
		long slot = tt->getSlot( &si );
		// loop over all dates in the "origin" section
		for (;slot >= 0 ; slot = tt->getNextSlot(slot,&si) ) {
			// get date in section "sk"
			Date *dd = *(Date **)tt->getValueFromSlot(slot);
			// skip if fuzzy
			if ( dd->m_flags & DF_FUZZY ) continue;
			// skip if a close date to fix
			// Daily 9-5 (and non-holiday Mondays in Jan. & Sept)
			if ( dd->m_flags & DF_CLOSE_DATE ) continue;
			// stop if bad
			if ( dd->m_hasType & DT_MONTH ) break;
			if ( dd->m_hasType & DT_DAYNUM ) break;
		}
		// skip if good
		if ( slot < 0 ) continue;
		// otherwise, mark as bad
		ev->m_flags |= EV_BAD_STORE_HOURS;
		// the total count
		//m_revisedValid--;
	}
	*/



	////////////////////////////////
	//
	// Set Event::m_eventId
	//
	// . so for this all to work, let's create eventIds for the
	//   surviving events, and then also determine what range of
	//   eventIds each section contains, so that Events::hash() can index
	//   terms in such sections with this range of eventIds. so
	//   we just telescope each valid event up and set the section's
	//   m_minEventId and m_maxEventId.
	//
	////////////////////////////////
	//m_numValidEvents = 0;
	//m_revisedValid   = 0;
	// start first eventId at one to avoid confusion
	long eid = 1;
	// always keep this NULL
	//m_validEvents[0] = NULL;
	m_idToEvent[0] = NULL;
	m_numIds = 0;
	// for sanity check
	Section *lastsp = NULL;
	// now when we index a term we telescope its section up until we
	// get a m_minEventId/m_maxEventId range so we can hash it
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// . skip it if it got disqualified
		// . now we are much more liberal with "banning" text
		//   from sections we thought to be events...
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// the event is often telescoped, i.e. consisting of a
		// bunch of different dates sewn together, possibly from
		// different sections, so let's check out those sections
		Date *ed = ev->m_date;

		// set it
		ev->m_eventId = eid;

		// get our event id as a byte offset and bit mask
		//unsigned char byteOff = ev->m_eventId / 8;
		//unsigned char bitMask = 1 << (ev->m_eventId % 8);

		// and scan over each one
		for ( long i = 0 ; i < ed->m_numPtrs ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get that date
			Date *di = ed->m_ptrs[i];
			// get section
			Section *sp = di->m_section;//ev->m_section;
			// . sanity check --need to be ordered for this to work
			// . eventIds need to be ordered by page location!!
			//if(lastsp&&sp->m_a<lastsp->m_a){char *xx=NULL;*xx=0;}
			// set it
			lastsp = sp;
			// now set all its parent sections min/max eventIdrange
			for ( ; sp ; sp = sp->m_parent ) {
				// add it
				sp->addEventId ( ev->m_eventId );
				// keep count
				//if ( ev->m_eventId < 256 &&
				//     !( sp->m_evIdBits[byteOff] & bitMask ) )
				//	sp->m_numEventIdBits++;
				// set our bit in the array of bits
				//if ( ev->m_eventId < 256 )
				//	sp->m_evIdBits[byteOff] |= bitMask;
				// when a Section class is created it is 
				// zero'ed out, see Sections.cpp for that.
				//if(sp->m_minEventId<=0||sp->m_minEventId>eid)
				//	sp->m_minEventId = eid;
				//if(sp->m_maxEventId<=0||sp->m_maxEventId<eid)
				//	sp->m_maxEventId = eid;
			}
		}
		// ptr for it!
		m_idToEvent[eid] = ev;
		// sanity check
		if ( eid >= MAX_EVENTS+1 ) { char *xx=NULL;*xx=0; }
		// advance it 
		eid++;
		// count number of good events
		//m_numValidEvents++;
		m_numIds++;
	}

	// all done if no good events found
	if ( eid == 1 ) return true;

	/////
	//
	// set "maxa"
	//
	/////
	long maxa = -1;
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// . skip it if it got disqualified
		// . now we are much more liberal with "banning" text
		//   from sections we thought to be events...
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// the event is often telescoped, i.e. consisting of a
		// bunch of different dates sewn together, possibly from
		// different sections, so let's check out those sections
		Date *ed = ev->m_date;
		// determine max
		if ( ed->m_maxa <= maxa ) continue;
		// got a new max
		maxa = ed->m_maxa;
	}

	////////////////////////////////////
	//
	// set SEC_TAIL_CRUFT on sections in the tail that should not
	// be a part of any event description.
	//
	// - look for certain keywords like copyright
	// - must be after all dates, except maybe pub date
	// - abqtango.com and unm.edu got hurt after we updated our
	//   pub date detector
	// - grab the last text on the page, blow up until right before
	//   it hits an event date (maxa) and see if it contains a keyword
	//   or menu or something, then call it all a tail.
	// - TODO: implement this algo!!
	// - we unfortunately get the tail for this
	// www.kumeyaay.com/2009/11/dragonfly-lecture-bighorn-sheep-songs/
	//
	////////////////////////////////////
	// 1. first get last alnumword in doc
	// 2. blow up until under "maxa"
	// 3. set SEC_HASNONFUZZY_DATE on it
	// 4. see if it helps/hurts us and if we can improve it...
	long lasta = -1;
	for ( long i = m_nw - 1 ; i >= 0 ; i-- ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not it
		if ( ! m_wids[i] ) continue;
		// got it
		lasta = i;
		break;
	}
	// blow it up
	Section *sa = NULL;
	Section *final = NULL;
	if ( lasta >= 0 ) sa = sp[lasta];
	if ( maxa < 0 ) sa = NULL;
	// . avoid tail detection for rss feeds
	// . hurts trumba.com's last tag..
	if ( isRSSExt ) sa = NULL;
	for ( ; sa ; sa = sa->m_parent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// stop if contains last event date
		if ( sa->m_a < maxa ) break;
		// record last section that does not contain event date
		final = sa;
	}
	// HACK: mark final as SEC_TAIL_CRAP
	if ( final )
		final->m_flags |= SEC_TAIL_CRAP;



	/////////////
	//
	// set SEC_PUBDATECONTAINER
	//
	/////////////
	//
	Date *prev = NULL;
	// . creates a partition for "articles" 
	// . so that if an article talks about an event it does not use the
	//   words from another article on the same page
	// . we do not allow event description sentence to telescope out
	//   of a pubdate container section. 
	// . pubdates are NOT considered nonfuzzy dates so the nonfuzzy logic
	//   in setting the event description is not good enough
	for ( long i = 0 ; i < nd ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// must be pubdate
		if ( ! ( flags & DF_PUB_DATE ) ) continue;
		// set this
		Date *last = prev;
		// update this
		prev = di;
		// skip if we had no prev
		if ( ! last ) continue;
		// ok, we got the next one, find the largest section
		// around each one without intersecting
		Section *s1 = last->m_section;
		Section *s2 = di  ->m_section;
		// blow up both until right before they intersect
		for ( ; s1 ; s1 = s1->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( ! s1->m_parent ) continue;
			if ( s1->m_parent->contains ( s2 ) ) break;
		}
		for ( ; s2 ; s2 = s2->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( ! s2->m_parent ) continue;
			if ( s2->m_parent->contains ( s1 ) ) break;
		}
		// set the flags
		s1->m_flags |= SEC_PUBDATECONTAINER;
		s2->m_flags |= SEC_PUBDATECONTAINER;
	}



	////////////////////////////////
	//
	// Set SEC_HAS_NONFUZZYDATE
	//
	////////////////////////////////
	for ( long i = 0 ; i < nd ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// get flag
		dateflags_t flags = di->m_flags;
		// skip if telescoped, we should only look at the
		// non-telescope dates.
		if ( di->m_hasType & DT_TELESCOPE ) continue;
		// . skip if bad format
		// . no, assume american because address is only in america
		//if ( flags & DF_AMBIGUOUS ) continue;
		// or clock
		if ( flags & DF_CLOCK ) continue;
		// must be from body
		if ( ! (flags & DF_FROM_BODY) ) continue;
		// must not be fuzzy
		if ( flags & DF_FUZZY ) continue;
		// do not set NONFUZZYDATE for trumba.com and rss feeds
		if ( (flags & DF_PUB_DATE) && isRSSExt ) continue;
		// . must not be a pub date
		// . why? this is hurting unm.edu etc.
		// . well, only ignore it if not at end of event dates now
		if ( (flags & DF_PUB_DATE) &&
		     // if it is below the last event date then it is
		     // like a copyright tail so in that case we want to
		     // set DF_HAS_NONFUZZY_DATE to limit its description
		     di->m_maxa < maxa ) continue;
		// or ticket date
		if ( flags & DF_NONEVENT_DATE ) continue;
		if ( di->m_flags5 & DF5_IGNORE ) continue;
		// a daynum range by itself is often kind of fuzzy but we do
		// not mark it as DF_FUZZY cuz we want to use it in telescopes
		if ( di->m_hasType == DT_RANGE_DAYNUM ) continue;
		// must not be a bad recurring dow
		//if ( flags & DF_BAD_RECURRING_DOW ) continue;
		// is also part of verified/inlined address?
		if ( m_bits[di->m_a] & D_IS_IN_ADDRESS ) continue;
		if ( m_bits[di->m_a] & D_IS_IN_VERIFIED_ADDRESS_NAME) continue;
		// sanity
		if ( di->m_a < 0 ) continue;
		// . let single years slide for this even if not fuzzy because
		//   a lot of event descriptions have a year in them that
		//   is referring to when the actor started or something
		// . fixes zvents.com/...kimo... 2/14/ 10pm
		if ( di->m_type == DT_YEAR ) continue;
		// "2001/2002 season" from events.kgoradio.com
		if ( di->m_hasType == (DT_YEAR|DT_LIST_ANY) ) continue;
		if ( di->m_hasType == (DT_YEAR|DT_RANGE_YEAR) ) continue;
		// "summer of 2001" from events.kgoradio.com
		// MDW: just use season???
		if ( di->m_hasType==(DT_YEAR|DT_SEASON|DT_COMPOUND)) 
			continue;
		if ( di->m_hasType==(DT_YEAR|DT_HOLIDAY|DT_COMPOUND)) 
			continue;
		if ( di->m_hasType==(DT_YEAR|DT_SUBDAY|DT_COMPOUND)) 
			continue;
		if ( di->m_hasType==(DT_YEAR|DT_SUBWEEK|DT_COMPOUND)) 
			continue;
		if ( di->m_hasType==(DT_YEAR|DT_SUBMONTH|DT_COMPOUND)) 
			continue;
		if ( di->m_hasType==(DT_YEAR|DT_EVERY_DAY|DT_COMPOUND)) 
			continue;
		if ( di->m_hasType==(DT_YEAR|DT_ALL_HOLIDAYS|DT_COMPOUND)) 
			continue;
		// . if just simple tod or tod range, let it slide so we can
		// . "kids race 10am, adult race 9am"
		// . http://nycday.eventbrite.com/ iphone boot camp tod range
		// . http://sunsetpromotions.com/events/139298/TRENTEMOLLER-L
		// . http://www.santafeplayhouse.org/onstage.php4
		//   - "lobby opens at 7pm"...
		// . http://www.guysndollsllc.com/
		// . http://www.residentadvisor.net/event.aspx?221238
		if ( di->m_hasType==DT_TOD) 
			continue;

		// nights ... 7pm for santafeplayhouse.org (lobby open time)
		// i guess makeCompounds linkInSameSent flag started working
		// and it connected those two dates. so we need this now.
		if ( di->m_hasType == (DT_TOD|DT_SUBDAY|DT_COMPOUND) )
			continue;

		// or maybe use this one?
		if ( di->m_flags & DF_CLOSE_DATE )
			continue;

		// . this was hurting mvusd.us
		// . now we have subeventbrothers logic so such beasties
		//   would be included in the summary anyhow, but not indexed.
		//   which i think is what we want.
		// . but rather than comment this out we should make that
		//   one section an event brother by identifying the "Hall"
		//   on a line by itself as a location. AND we should not pick
		//   a title that has a date in it that isn't ours!!!
		// . we do lose the restaurant hours in guysndollsllc.com 
		//   because i commented this out...
		//if ( di->m_hasType==(DT_RANGE_TOD|DT_TOD)) 
		//	continue;

		// fix for gitscperfectpreso.eventbrite.com
		// Created in  February of 2007, 'Girls in Tech'
		if ( di->m_hasType==(DT_MONTH|DT_YEAR|DT_COMPOUND) )
			continue;
		// events.sfgate.com...exploratorium.. has an event title
		// that is "Special Holiday Hours" so do not allow those
		// to be non fuzzy
		if ( di->m_type & ( DT_HOLIDAY      |
				    DT_SUBDAY       |
				    DT_SUBWEEK      |
				    DT_SUBMONTH     |
				    DT_EVERY_DAY    |
				    DT_SEASON       |
				    DT_ALL_HOLIDAYS ))
		     continue;

		// get its section
		Section *si = m_sections->m_sectionPtrs[di->m_a];

		// "Sun Van Bus Service" (unm.edu)
		if ( di->m_hasType == DT_DOW ) {
			// assume it is strong
			bool strongDow = true;
			// shortcut
			sec_t secflags = si->m_flags;
			// . i did all this to fix speckledts.com
			// . "Mondays" is a heading container in 
			//   speckledts.com and the paragraph under it should 
			//   not cross pollinate with the event date under 
			//   Tuesdays header.
			// . eventbrothers were not being set because of lack 
			//   of consistent structure and tods under each dow 
			//   header
			// . but for "Sunday Brunch" in speckledts.com, that is
			//   a weak dow because it might be a band name, but,
			//   it is in a heading container! however, we do not
			//   want to include it in our event title/descr algo.
			if ( (  di->m_flags & DF_HAS_WEAK_DOW ) &&
			     ! ( di->m_flags & DF_HAS_STRONG_DOW ) &&
			     ! ( secflags & SEC_HEADING_CONTAINER ) )
				strongDow = false;
			// fix residentadvisor's "2nd Sundays" in the menu. it
			// was closer to the event description than the event 
			// date was and was causing us to lose the description.
			// so if dow is in a menu, share it.
			if ( secflags & SEC_MENU ) 
				strongDow = false;
			// . if not a strong dow, then do not set 
			//   SEC_HAS_NONFUZZYDATE
			// . if not a strong dow, we still want it in the
			//    event descr.
			if ( ! strongDow ) continue;
		}

		// . "8" a single daynum with nothing else
		// . fixes yelp.com for blackbird buvette which has
		//   "<tag>8</tag> ratings"
		if ( di->m_hasType == DT_DAYNUM && di->m_suppFlags == 0 ) 
			continue;
		// "1-11" is fuzzy
		if ( di->m_hasType == (DT_DAYNUM|DT_RANGE_DAYNUM) )
			continue;
		// set all above
		for ( ; si ; si = si->m_parent )
			si->m_flags |= SEC_HAS_NONFUZZYDATE;
	}

	////
	//
	// set EVENT_BROTHER bits a 3rd time now that we have implied sections
	//
	// . should fix gwair.org whose implied sections are event brothers
	// . fixes other urls too i guess
	//
	////
	//m_dates->setEventBrotherBits();
	
	/////////////////
	//
	// set SEC_IGNOREEVENTBROTHER on sections
	//
	/////////////////

	// . set SEC_IGNOREEVENTBROTHER bit on address sections, etc.
	// . do this first so the loop below can access the eventids we set
	// . sometimes we lose the address and description around it, because
	//   it is in a datebrother section. so in this special case assign
	//   the address date brother section to the event description
	// . now take the address section of each event and force that to
	//   be part of the event description if not already
	// . very frequently an address section is not part of the description
	//   for an event because it is in a datebrother section, like
	//   the zvents.com url for terrence wilson has a formation like:
	//   <div>date1</div><div>date2s</div><div>desc+addr</div> and each
	//   of those 3 brother sections had datebrother set because they
	//   seemed likely to be separate events. (i.e. two brother sections
	//   had a day-of-month (dom) and a time-of-day (tod))

	// . scan all event brother lists
	for ( Section *si = m_sections->m_rootSection ; si ; si = si->m_next){
		// breathe
		QUICKPOLL(m_niceness);
		// must be event brother
		if ( ! ( si->m_flags & SEC_EVENT_BROTHER ) ) continue;
		// must be first in list
		Section *pb = si->m_prevBrother;
		// if there and also has bit set, forget it
		if ( pb && (pb->m_flags & SEC_EVENT_BROTHER) ) continue;

		long phoneXor   = 0;
		long emailXor   = 0;
		long priceXor   = 0;
		long todXor     = 0;
		long addrXor = 0;

		bool diffPhones = false;
		bool diffEmails = false;
		bool diffPrices = false;
		bool diffDates  = false;
		bool diffAddrs  = false;

		Section *sj = si;
		Section *lastBro = NULL;
		// identify which xors are in disagreement
		for ( ; sj ; sj = sj->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// see if xor in disagreement
			if ( sj->m_phoneXor && phoneXor &&
			     sj->m_phoneXor != phoneXor && phoneXor ) 
				diffPhones = true;
			if ( sj->m_emailXor && emailXor &&
			     sj->m_emailXor != emailXor && emailXor ) 
				diffEmails = true;
			if ( sj->m_priceXor && priceXor &&
			     sj->m_priceXor != priceXor && priceXor ) 
				diffPrices = true;
			if ( sj->m_todXor && todXor &&
			     sj->m_todXor != todXor && todXor ) 
				diffDates = true;
			if ( sj->m_addrXor && addrXor &&
			     sj->m_addrXor != addrXor && addrXor ) 
				diffAddrs = true;
			// assign otherwise
			if ( sj->m_phoneXor ) phoneXor = sj->m_phoneXor;
			if ( sj->m_emailXor ) emailXor = sj->m_emailXor;
			if ( sj->m_priceXor ) priceXor = sj->m_priceXor;
			if ( sj->m_todXor   ) todXor   = sj->m_todXor;
			if ( sj->m_addrXor  ) addrXor  = sj->m_addrXor;
			// remember last brother
			lastBro = sj;
		}

		// set these to impossible values
		if ( diffPhones ) phoneXor = -1;
		if ( diffEmails ) emailXor = -1;
		if ( diffPrices ) priceXor = -1;
		if ( diffDates  ) todXor   = -1;
		if ( diffAddrs  ) addrXor  = -1;

		// reset
		sj = lastBro;

		// now scan backwards set SEC_IGNOREEVENTBROTHER if the brother
		// has a xor that does not match the *Xor value.
		for ( ; sj ; sj = sj->m_prevBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if bad
			//if(sj->m_phoneXor && sj->m_phoneXor!=phoneXor) break;
			//if(sj->m_emailXor && sj->m_emailXor!=emailXor) break;
			//if(sj->m_priceXor && sj->m_priceXor!=priceXor) break;
			//if(sj->m_todXor   && sj->m_todXor  !=todXor  ) break;
			if ( sj->m_addrXor  && sj->m_addrXor !=addrXor ) break;
			// . only allow addr sections to be used in description
			// . if it has no address usually its just cruft like
			//   "To view source code..." or 
			//   "Email us if interested"
			if ( ! sj->m_addrXor ) continue;
			// undo it otherwise
			sj->m_flags |= SEC_IGNOREEVENTBROTHER;
			// stop after one address
			break;
		}
	}

	// . there are bad section flags
	// . do not include SEC_DUP since page might be mirrored (use SEC_MENU)
	sec_t badFlags2 = 
		SEC_MARQUEE|
		SEC_SELECT|
		SEC_SCRIPT|
		SEC_STYLE|
		SEC_NOSCRIPT|
		SEC_HIDDEN|
		SEC_STRIKE| // <strike>
		SEC_STRIKE2| // <s>
		//SEC_MENU|
		//SEC_MENU_HEADER|
		SEC_INPUT_HEADER|
		SEC_INPUT_FOOTER;

	// short cut
	long np = m_addresses->m_numSorted;

	///////////////////////////////////////////
	//
	// *** SET THE EVENT DESCRIPTIONS ***
	//
	// Set Event::m_minEventId/m_maxEventId for other text sections
	//
	// . determine what event descriptions they go with
	//
	///////////////////////////////////////////
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// stop if no words!
		if ( ss->m_numSections == 0 ) break;
		// breathe
		QUICKPOLL(m_niceness);
		// get section ptr
		//Section *si = ss->m_sorted[i];//sections[i];
		// skip if no text
		//if ( si->m_flags & SEC_NOTEXT ) continue;
		// don't break sentences apart now we are sentence based
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if bad
		if ( si->m_flags & badFlags2 ) continue;
		// skip if already assigned
		if ( si->m_minEventId >= 1 ) continue;

		// if all children sections in this sentence section have
		// a bad flag set, then skip it as well. fixes
		// "events venues restaurants movies performaers" for
		// zvents.com
		bool hasGood = false;
		Section *cs = si;
		for ( ; cs && si->contains(cs) ; cs = cs->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if bad
			if ( cs->m_flags & badFlags2 ) continue;
			// must have text DIRECTLY
			if ( cs->m_flags & SEC_NOTEXT ) continue;
			// we are ok otherwise
			hasGood = true;
			break;
		}
		if ( ! hasGood ) continue;

		// reset this
		Place *placePtr = NULL;
		// skip if was thought to be an event section but got
		// cancelled for some reason. its date might have been in
		// the past...
		//if ( ev->m_flags & EV_OLD_EVENT   ) continue;
		//if ( ev->m_flags & EV_EMPTY_TIMES ) continue;
		// the telescope parent ptr
		Section *sp = si;
		// reset this
		Section *todSec = NULL;
		// now telescope out until we either hit ht or hit
		// a parent section that has a valid event id range
		for ( ; sp ; sp = sp->m_parent ) {
			// . stop if it has an event already
			// . TODO: allow it if it just contains OUR EVENT
			// . do this check before the one below!!!!
			if ( sp->m_minEventId >=1 ) break;
			// our new logic:
			// now that we made the tail date a pub date for
			// unm.edu, we no longer encounter this flag.
			// i would day pub dates should though!!!
			// - pubdates are considered fuzzy!
			// i'd like to comment this out, but won't then i
			// get unrecognized pub/comment dates in the
			// event description and their associated comments?
			if ( sp->m_flags & SEC_HAS_NONFUZZYDATE ) break;
			// now we can't leave a pub date container which
			// is supposed to partition articles on a page,
			// so we don't get text from one article in another
			// that is talking about an event.
			if ( sp->m_flags & SEC_PUBDATECONTAINER ) break;
			// are we part of a tail section?
			if ( sp->m_flags & SEC_TAIL_CRAP ) break;
			// stop if it a form tag. if in an isolated form
			// tag, do not allow it to telescope out and meet
			// an event outside of this form tag.
			if ( sp->m_tagId == TAG_FORM ) break;
			// same goes for iframe tags
			if ( sp->m_tagId == TAG_IFRAME ) break;
			// same goes for table headers
			if ( sp->m_tagId == TAG_TH ) break;

			// . this stops it cold
			// . if each brother of sp is probably an event, stop.
			// . we still need to deal with tags like:
			//   "<p>tod1</p><p>tod2</p><p>desc</p>" so the
			//   desc can apply to tod2. this should fix the
			//   guysndollsllc.com root page for "all you can fish"
			//   which is the "desc" in this case but it is NOT
			//   in a datebrother section, but is in a todevent
			//   section.
			if ( (sp->m_flags & SEC_EVENT_BROTHER) &&
			     // there are exceptions as set above for tailing
			     // event brothers that are likely additional
			     // information that applies to all events above
			     !(sp->m_flags & SEC_IGNOREEVENTBROTHER) ) break;

			// stop telescoping if we have SEC_TOD_EVENT set
			// because that means we are a brother section
			// in a list of events. although we ourselves may
			// not be recognized as an event, we often are a
			// failed event, that had a strange date or location
			// or none at all, just a "to be announced", so better
			// safe than sorry and ignore these. we set this
			// section bit in Dates.cpp. we also use to to avoid
			// telescoping between such labeled brothers as well.
			//if ( sp->m_flags & SEC_TOD_EVENT ) {
				// but if he is a heading and our previous
				// brother allow him!
				//if ( sp->m_nextBrother == 
				//if (!(sp->m_flags & SEC_HEADING_CONTAINER) &&
				//     !(sp->m_flags & 
				//       SEC_NIXED_HEADING_CONTAINER) )
				//	break;
				// the above logic was causing 
				// "Children's classes" for texasdrums.org
				// to be a heading for all SEC_TOD_EVENT
				// sections even though it was heading just
				// one of such sections. this was not a
				// problem until we allowed SEC_HEADING to be
				// set even if one word was lowercase by
				// adding the "lowerCount" parm in Sections.cpp
				// And now we deal with todSec below and
				// such heading sections can only be in the
				// description of events BELOW them up UNTIL
				// we hit another such heading section! this
				// seems like it should fix salsapower.com too
				// which has the "DOUBLE HEADER" bug mentioned
				// in Dates.cpp. sometimes headings do not
				// form an implied section like they should.
				// could we move this logic into the implied
				// section generator? then why isn't texasdrums
				// getting that section? because the first
				// header row contains 3 heading sections
				// and the next header row has only 
				// "Children's classes" so it is the only
				// heading section seen i guess. so this logic
				// here is kind of a fallback.
				// MDW: implied sections should fix this,
				// so comment these two lines out...
				// and it was removing valid descriptions
				// from other events...
				// WELL, we still need to break because
				// todsections in unm.edu are events themselves
				// although datebrother is not set... but
				// todevent is set...
				//todSec = sp;
				//break;
			//}

			// . or if section tag hash has contained an address
			//   or tod elsewhere, we should also limit it??
			// . for www.switchboard.com/albuquerque-nm/doughnuts/
			//   the other entries in the yellow pages had no
			//   "nonfuzzydates" and thus were telescoping up
			//   until they hit the event id for "a b c bakery"
			//   and thus were part of that description. so as
			//   soon as we hit a section containing an address,
			//   stop telescoping the text.
			// . TODO: if the address belongs to an event then
			//   we might allow it to be used for all events
			//   that use that address
			//if ( sp->m_numAddresses >= 1 ) break;
			// same for streets!
			//if ( sp->m_numPlaces >= 1 ) break;
			//if ( st->isInTable ( &sp ) ) break;
			// mix key
			//long key = hash32h((long)sp,456789);
			//if ( ! at->isInTable ( &key ) ) continue;
			long spi = sp->m_firstPlaceNum;
			// if no places are contained in "sp" be on our way
			if ( spi < 0 ) continue;

			//
			// this logic allows "Mabiba..." from 
			// texasdrums.drums.org to make it into the event
			// descriptions because it is in the address section
			//

			// is it our address? (this is the TODO above)
			//Place *p = *(Place **)at->getValue ( &key );
			Place *p = m_addresses->m_sorted[spi];
			// set it i guess
			if ( ! placePtr ) placePtr = p;
			// if already had a place, that's it, stop
			//if ( placePtr != p ) break;

			// check all places in this section, must only be one
			//long slot = at->getSlot ( &key );
			// must be there
			//if ( slot < 0 ) { char *xx=NULL;*xx=0; }
			// can only have one address/place i guess!
			//for( ; slot >= 0; slot=at->getNextSlot(slot,&key) ) {
			for ( ; spi < np ; spi++ ) {
				//Place*p2=*(Place**)at->getValueFromSlot(slot)
				Place *p2 = m_addresses->m_sorted[spi];
				// stop if breach
				if ( p2->m_a >= sp->m_b ) break;
				// sanity
				if ( p2->m_a < sp->m_a ) {char *xx=NULL;*xx=0;}
				// . skip if po box, ignore those
				// . now that we ignore them for getting
				//   event locations above, we need to ignore
				//   them here too
				if ( p2->m_flags2 & PLF2_IS_POBOX ) continue;
				// keep going if this place is the same place
				if ( p2->m_hash == placePtr->m_hash ) continue;
				// if this place aliases a full address then
				// get the hash from that
				if ( placePtr->m_alias &&
				     placePtr->m_alias->m_street->m_hash ==
				     p2->m_hash )
					continue;
				if ( p2->m_alias &&
				     placePtr->m_hash == 
				     p2->m_alias->m_street->m_hash )
					continue;
				// if placePtr is a place name
				if ( placePtr->m_address &&
				     placePtr->m_address->m_street->m_hash ==
				     p2->m_hash )
					continue;
				// or vice versa
				if ( p2->m_address &&
				     p2->m_address->m_street->m_hash ==
				     placePtr->m_hash )
					continue;
				// . if we hit a lat-lon only address then 
				//   let it slide for now. 
				// . do not treat as distinct address for 
				//   this algo
				// . because the lat/lon associated with
				//   a place is often repeated and the repeats
				//   won't get associated with the place 
				//   because one of the other closer repeats 
				//   claimed it first. anyway, the repeats 
				//   might not all be exactly the same either.
				// . without this dmjuice.com/167293 was
				//   losing its address as part of the event
				//   descr
				if ( p2      ->m_type == PT_LATLON ) continue;
				if ( placePtr->m_type == PT_LATLON ) continue;
				// flag it
				spi = -1;
				// otherwise, assume new place
				break;
			}
			// if had multiple different places, stop!
			//if ( slot >= 0 ) break;
			if ( spi == -1 ) break;
			// store it
			//placePtr = p;

			// if we were basically an event section because
			// SEC_HAS_DATE was true, but we did not have an
			// address, do not allow our text to be part of
			// this other event's description. prevents
			// http://www.dailylobo.com/calendar/ from getting
			// other events as the one event's description.
			//if (  (sp->m_flags & SEC_HAS_NON_EVENT_DATE) &&
			//     !(sp->m_flags & SEC_HAS_EVENT_DATE    ) )
			//	break;
		}
		// bail if nothing
		if ( sp->m_minEventId <= 0 && ! todSec ) continue;

		// sibling?
		//if ( ht.isInTable ( &sp->m_tagHash ) )
		//	break;
		// if it does not contain our event section, bail on it
		if ( sp->m_a >  si->m_b ) { char *xx=NULL;*xx=0; }
		if ( sp->m_b <= si->m_a ) { char *xx=NULL;*xx=0; }
		// set the range of event ids
		long minEventId = sp->m_minEventId;
		long maxEventId = sp->m_maxEventId;


		// now make sure all those events have our place
		if ( placePtr && ! todSec ) {
			// get address
			Address *aa = placePtr->m_address;
			if ( ! aa ) aa = placePtr->m_alias;
			// if not part of address, do not allow
			if ( ! aa ) continue;
			// skip if doesn't match all event addresses
			long e;
			// scan event ids
			for ( e = minEventId ; e <= maxEventId ; e++ ) {
				// get event
				Event *ev = m_idToEvent[e];
				// check address
				//if ( ev->m_address != aa ) break;
				// seems like aa was referencing a placedb rec
				// for trumba.com for the title
				// "White Christmas at the Abq Little Theater"
				// since it was a name it looked up in placedb
				// so let's use the hash here
				if ( ev->m_address->m_hash == aa->m_hash ) 
					continue;
				// denver.org has "10035 south peoria" and
				// repeats "10035 south peoria street" so
				// addr hash is off, but street hash is same!
				if ( ev->m_address->m_street->m_hash ==
				     aa->m_street->m_hash )
					continue;
				// ignore if lat lon only. do not treat
				// as distinct address for this algo
				if ( aa->m_flags3 & AF2_LATLON )
					continue;
				//if ( ev->m_address->m_flags3 & AF2_LATLON )
				//	continue;
				// not the same!
				break;
			}
			// if no match for any event, stop it!
			if ( e <= maxEventId ) continue;
			// otherwise, allow it through
		}

		if ( ! todSec ) {
			// assign event range to this text section
			si->m_minEventId = minEventId;
			si->m_maxEventId = maxEventId;
			// and copy over the bits too
			memcpy(si->m_evIdBits,sp->m_evIdBits,8*4);
			// and the number
			si->m_numEventIdBits = sp->m_numEventIdBits;
			continue;
		}

		// topological ties.
		//
		// similar to tied title scores in Events::setTitle()
		//
		// . when a descriptive section has brother sections with 
		//   eventids we are unsure for which eventids this 
		//   descriptive section applies. 
		// . if todSec is sandwiched by 2 events then do not
		//   apply to either since it is not clear...
		// . so if we had something like:
		//   <p>tod1</p><p>tod2</p><p>desc</p> then
		//   "desc" should only apply to the event at tod2, not tod1
		// . should fix guysndolls "all you can fish" desc.
		//

		// undo our max min
		minEventId = 0;
		maxEventId = 0;
		bzero(sp->m_evIdBits,8*4);


		// first scan backward then forward looking for minEventId>0
		// ...

		// if we are not a heading section, then this
		// will not work either... this fixes texasdrums.org
		// which had one row that had 3 headings in it, but
		// the row itself did not have SEC_HEADING_CONTAINER
		// set... "Dance classes, African Drum classes, ..."
		// -- no longer needed since we have table swoggling
		//if ( ! ( todSec->m_flags & SEC_HEADING_CONTAINER ) )
		//	// bail on it, it is probably its own
		//	// event section
		//	continue;
		// skip past us otherwise
		todSec = todSec->m_nextBrother;

		// if we hit the SEC_TOD_EVENT section, only apply this
		// heading to eventids BELOW us!
		for ( ; todSec ; todSec = todSec->m_nextBrother ) {
			// breathe
			QUICKPOLL(m_niceness);
			// MDW: try taking this out
			// if its a heading too! stop the event id accumulator
			//if ( (todSec->m_flags & SEC_HEADING_CONTAINER) &&
			//     // sometimes the sentence with the event tod
			//     // also has the heading bit set, so do not count
			//     // that as a heading for this breaking purpose.
			//     todSec->m_minEventId<= 0 ) 
			//	break;
			// also break if we hit a blank tag, a section 
			// delimeter, otherwise "Clubs - Albuquerque" for
			// salsapower.com propagated to all events below since
			// the <tr>'s ended up in their own implied sections
			// because of the empty tag delimeters adding sections
			// in addImpliedSections(), so the "Club-Albq" brother
			// never hit another SEC_HEADING_CONTAINER. so let's
			// stop propagating this heading if we hit a
			// blank tag that will cause an implied section
			if ( todSec->m_firstWordPos < 0 ) break;
			// or for that matter, any implied section
			if ( todSec->m_flags & SEC_FAKE ) break;
			// skip if not a heading and has no event id either
			if ( todSec->m_minEventId <= 0 ) continue;
			// . otherwise, we must be a heading for this event
			//   so add these eventids to sp's min/max and
			//   sp's m_evIdBits array
			// . dst=sp src=todSec
			addEventIds ( si , todSec );
			// . to be on the safe side stop after this
			// . hurts unm.edu cuz that title is meant for
			//   all the sections below it, but helps
			//   graffiti.org where "cody hudson" is only meant
			//   for the tod section right below it. no, this
			//   hurts salsapower.com etc. so ...
			// . unm.edu should be fixed! so try uncommenting:
			break;
		}
		// we are done dealing with tod sections
		//if ( orig ) continue;


		// assign event range to this text section
		//si->m_minEventId = minEventId;
		//si->m_maxEventId = maxEventId;
		// and copy over the bits too
		//memcpy(si->m_evIdBits,sp->m_evIdBits,8*4);
		// and the number
		//si->m_numEventIdBits = sp->m_numEventIdBits;
	}


	////////////////////
	//
	// EVENT DESCRIPTION SHARING, part 1
	//
	// merge events that are really the same thing.
	//
	// A highly specialized algorithm.
	//
	// . http://rialtotheatre.ticketforce.com/default.asp
	//   repeats the day/month/year of the event in an
	//   adjacent brother section, so this was causing
	//   us to miss out on the event title.
	//
	// . http://www.eviesays.com/tickets/when/anytime/where/
	//   Albuquerque%2C%20NM/start/80.html
	//   also has a table where the event title sometimes has
	//   "Saturday Afternoon" and we miss it!
	//
	// . can this help us fix 
	//   www.nelsoncountylife.com/2009/11/24/koda-kerl-w-freeman-mowrer/ 
	//   which misses the good title because the date is repeated/
	//
	// . basically find dates not in event descriptions and see if
	//   they are basically a duplicate date for an adjacent section
	//   that is part of an event description.
	// . if the duplicate date has its own address/place, or is in its own
	//   "date brother" section, or was in its own "bad" event, then we do
	//   not pair it up
	// . this algo slows us down by like 0.3%
	//
	for ( long i = 0 ; i < m_dates->m_numDatePtrs ; i++ ) {
		//break;
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// do not do telescopes yet
		if ( di->m_hasType & DT_TELESCOPE ) break;

		char qualified = 0;
		// must have month daynum for rialtotheatre case
		if ((di->m_hasType&(DT_MONTH|DT_DAYNUM))==(DT_MONTH|DT_DAYNUM))
			qualified = 1;
		// and a dow with a DT_SUBDAY for eviesays
		if ((di->m_hasType&(DT_DOW|DT_SUBDAY))==(DT_DOW|DT_SUBDAY))
			qualified = 2;
		// if not one of those dates then skip it
		if ( ! qualified ) continue;
		//if ( ! (di->m_hasType & DT_MONTH  ) ) continue;
		//if ( ! (di->m_hasType & DT_DAYNUM ) ) continue;
		// . no tod or location
		// . this hurts nelsoncountylife.com because the adjacent
		//   date is a full date, repeated in full
		//if ( di->m_flags & DF_EVENT_CANDIDATE ) continue;

		// get his section
		Section *sd = di->m_section;
		// if none cuz from a url then forget it
		if ( ! sd ) continue;
		// blow up until right before hits an event desc section
		for ( ; sd ; sd = sd->m_parent ) 
			// stop at sentence section
			if ( sd->m_flags & SEC_SENTENCE ) break;
		// if already part of event description, skip it
		if ( sd->m_minEventId > 0 ) continue;

		// now telescope up until we hit a description section
		for ( ; sd ; sd = sd->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop when we hit this
			if ( sd->m_minEventId > 0 ) break;
			// . stop at date brother too!
			// . this hurts nelsoncountylife.com too because
			//   we need to fix the todxor so they are equal
			if ( sd->m_flags & SEC_EVENT_BROTHER ) break;
			// stop at addr too
			//long key = hash32h((long)sd,456789);
			//if ( at->isInTable(&key) ) break;
			if ( sd->m_firstPlaceNum >= 0 ) break;
		}

		// if none every forget it
		if ( ! sd ) continue;
		// if did not make it to event desc, forget it too
		if ( sd->m_minEventId <= 0 ) continue;

		// how is this?
		if ( sd->m_firstDate <= 0 ) { char *xx=NULL;*xx=0; }

		// get a list of all dates in sd that are in the
		// current event description. the sideline date we want
		// to incorporate, must match ALL these dates.
		long da = sd->m_firstDate - 1; // 0;//sd->m_datea;
		long db = m_dates->m_numDatePtrs;//sd->m_dateb;
		long dayNumToMatch = -1;
		long monthToMatch  = -1;
		long dowToMatch    = -1;
		long k;
		for ( k = da ; k < db ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get date #k
			Date *dk = m_dates->m_datePtrs[k];
			// skip empties
			if ( ! dk ) continue;
			// stop if breach
			if ( dk->m_a >= sd->m_b ) break;
			// skip if before
			if ( dk->m_a < sd->m_a ) continue;
			// stop if telescope
			if ( dk->m_hasType & DT_TELESCOPE ) break;
			// get the date's containing section
			Section *sk = dk->m_section;
			// if date is not part of event description, skip it
			if ( sk->m_minEventId <= 0 ) continue;
			// mine the month and daynum from the date
			if ( dk->m_hasType & DT_MONTH ) 
				monthToMatch = dk->m_month;
			if ( dk->m_hasType & DT_DAYNUM )
				dayNumToMatch = dk->m_dayNum;
			if ( dk->m_hasType & DT_DOW )
				dowToMatch = dk->m_dow;
		}
		// must have had something to match
		if ( qualified == 1 ) {
			if ( monthToMatch  == -1 ) continue;
			if ( dayNumToMatch == -1 ) continue;
		}
		if ( qualified == 2 ) {
			if ( dowToMatch == -1 ) continue;
		}
		// did we get a match
		bool hadMatch = false;
		// now make sure all the other dates match that
		for ( k = da ; k < db ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get date #k
			Date *dk = m_dates->m_datePtrs[k];
			// skip empties
			if ( ! dk ) continue;
			// stop if breach
			if ( dk->m_a >= sd->m_b ) break;
			// skip if before
			if ( dk->m_a < sd->m_a ) continue;
			// stop if telescope
			if ( dk->m_hasType & DT_TELESCOPE ) break;
			// skip if fuzzy, like "2"
			if ( dk->m_flags & DF_FUZZY ) continue;
			// get the date's containing section
			Section *sk = dk->m_section;
			// if date is part of event description, skip it
			if ( sk->m_minEventId > 0 ) continue;
			// assume no match
			hadMatch = false;
			// make sure it matches the month/daynum of dates
			// that were in the event description
			if ( qualified == 1 ) {
				if ( ! (dk->m_hasType & DT_MONTH) )  break;
				if ( dk->m_month != monthToMatch ) break;
				if ( ! (dk->m_hasType & DT_DAYNUM ) ) break;
				if ( dk->m_dayNum != dayNumToMatch ) break;
			}
			if ( qualified == 2 ) {
				if ( ! (dk->m_hasType & DT_DOW ) ) break;
				if ( dk->m_dow != dowToMatch ) break;
			}
			// hey we got it
			hadMatch = true;
		}
		// if we did not match all month/daynums of dates in the
		// event description, we failed, and are not a date alias
		if ( ! hadMatch ) continue;
		// tag all sections in "sd" that are not in already
		// tagged as part of an event
		Section *sx = sd;
		// otherwise, consider all these dates to be non fuzzy and
		// part of their neighboring event descriptions
		//Section *sx = di->m_section;
		// blow up until we hit "sd"
		for (;sx && sx->m_a<sd->m_b; sx=sx->m_next) { //sx->m_parent){
			// breathe
			QUICKPOLL(m_niceness);
			// skip if already part of event desc
			if ( sx->m_minEventId > 0 ) continue;
			// add to event descriptions
			addEventIds ( sx , sd );
		}

	}

	///////////////
	//
	// EVENT DESCRIPTION SHARING, part 2
	//
	// fix events.thegazette.com
	//
	// . events.thegazette.com it has two sections
	//   which are basically the same event. one has
	//   "Wednesday, Oct 26, 2011 * 7:30 pm - 1:30 am" and the other has
	//   "every Wednesday Night [[]] 8:30 pm"
	//
	// . so we need to share so we can get the correct event title
	//   because it is often only in one section of a repeated event date
	//
	Date *lastDate = NULL;
	for ( long i = 0 ; i < m_dates->m_numDatePtrs ; i++ ) {
		//break;
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		//continue;
		// get prev date
		Date *prev = lastDate;
		// update it
		lastDate = di;
		// need it
		if ( ! prev ) continue;

		// get two containing sections
		Section *s1 = prev->m_section;
		Section *s2 = di  ->m_section;
		// must be dates in the doc, not in the url
		if ( ! s1 ) continue;
		if ( ! s2 ) continue;

		// blow up until right before containing each other
		for ( ; s1 ; s1 = s1->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( ! s1->m_parent ) break;
			if ( s1->m_parent->contains ( s2 ) ) break;
		}
		for ( ; s2 ; s2 = s2->m_parent ) {
			QUICKPOLL(m_niceness);
			if ( ! s2->m_parent ) break;
			if ( s2->m_parent->contains ( s1 ) ) break;
		}

		// . must be brother sections
		// . no, because delilah's lounge won't work then because
		//   although s1 is a nice section, s2 is after a few
		//   description sentences under s1.
		if ( s1->m_nextBrother != s2 ) continue;
		
		// both must have an event id
		if ( s1->m_minEventId <= 0 ) continue;
		if ( s2->m_minEventId <= 0 ) continue;

		// now any more brother sections must be date-less
		//Section *bro = s2->m_nextBrother;
		//for ( ; bro ; bro = bro->m_nextBrother ) {
		//	QUICKPOLL(m_niceness);
		//	if ( bro->m_flags & SEC_HAS_TOD ) break;
		//	if ( bro->m_flags & SEC_HAS_DOM ) break;
		//	if ( bro->m_flags & SEC_HAS_DOW ) break;
		//}
		// if any other brother section had a date, forget it!
		// we can only deal with two right now
		//if ( bro ) continue;
		// scan backwards too!
		//bro = s1->m_prevBrother;
		//for ( ; bro ; bro = bro->m_prevBrother ) {
		//	QUICKPOLL(m_niceness);
		//	if ( bro->m_flags & SEC_HAS_TOD ) break;
		//	if ( bro->m_flags & SEC_HAS_DOM ) break;
		//	if ( bro->m_flags & SEC_HAS_DOW ) break;
		//}
		// if any other brother section had a date, forget it!
		// we can only deal with two right now
		//if ( bro ) continue;

		// sanity check
		if ( s1->m_firstDate <= 0 ) { char *xx=NULL;*xx=0; }

		// 1. addresses must match of all event ids in s1 with
		//    all event ids in s2
		if ( s1->m_addrXor &&
		     s2->m_addrXor &&
		     s1->m_addrXor != s2->m_addrXor ) continue;

		// . if already share event ids then stop
		// . fixes la-bike.org from interpollincating subevent bros
		//   just because they all are in one section and the dom
		//   header date is their header and ended up doing this
		//   algo with them, and making them all share each other's
		//   desc.
		if ( s1->m_minEventId >= s2->m_minEventId &&
		     s1->m_minEventId <= s2->m_maxEventId ) 
			continue;
		if ( s1->m_maxEventId >= s2->m_minEventId &&
		     s1->m_maxEventId <= s2->m_maxEventId ) 
			continue;

		// get a list of all dates in s1 and set vars to match
		// for the dates in s2
		long da = s1->m_firstDate - 1;
		long db = m_dates->m_numDatePtrs;
		long dayNumToMatch = -1;
		long monthToMatch  = -1;
		long dowToMatch    = -1;
		long todMin        = -1;
		long todMax        = -1;
		long multMonths    = 0;
		long multDayNums   = 0;
		long multDows      = 0;
		long multTods      = 0;
		long k;
		for ( k = da ; k < db ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get date #k
			Date *dk = m_dates->m_datePtrs[k];
			// skip empties
			if ( ! dk ) continue;
			// skip if before
			if ( dk->m_a < s1->m_a ) continue;
			// stop if breach
			if ( dk->m_a >= s1->m_b ) break;
			// skip copyright dates, etc.
			if ( dk->m_flags & DF_CLOCK ) continue;
			// must not be fuzzy
			if ( dk->m_flags & DF_FUZZY ) continue;
			// no pub dates
			if ( dk->m_flags & DF_PUB_DATE ) continue;
			// mine the month and daynum from the date
			if (dk->m_hasType&DT_MONTH) {
				if ( monthToMatch != -1 &&
				     monthToMatch != dk->m_month ) 
					multMonths++;
				monthToMatch = dk->m_month;
			}
			if (dk->m_hasType&DT_DAYNUM) {
				if ( dayNumToMatch != -1 &&
				     dayNumToMatch != dk->m_dayNum )
					multDayNums++;
				dayNumToMatch = dk->m_dayNum;
			}
			if (dk->m_hasType & DT_DOW ) {
				if ( dowToMatch != -1 &&
				     dowToMatch != dk->m_dow )
					multDows++;
				dowToMatch   = dk->m_dow;
			}				
			if (dk->m_hasType & DT_TOD ) {
				if ( todMin != -1 &&
				     todMin != dk->m_minTod )
					multTods++;
				todMin = dk->m_minTod;
			}
			if (dk->m_hasType & DT_RANGE_TOD ) {
				todMin = dk->m_minTod;
				todMax = dk->m_maxTod;
			}
		}

		// this logic fixes www.seattle24x7.com
		if ( multMonths > 0  ) continue;
		if ( multDayNums > 0 ) continue;
		if ( multDows > 0    ) continue;
		if ( multTods > 0    ) continue;

		bool gotIt = false;
		// eventgazette.com has month/day to match in s1
		if ( monthToMatch  >= 0 && dayNumToMatch >= 0 ) gotIt = true;
		// this seems to merge pay lake sundays for guysndollsllc.com
		// into the main store hours, and we lose that good title.
		//if ( dowToMatch    >= 0 && todMin        >= 0 ) gotIt = true;
		// so we need at least one of those in s1
		if ( ! gotIt ) continue;

		// did we get a match
		bool noMatchMonth  = false;
		bool noMatchDayNum = false;
		bool noMatchTod    = false;
		bool noMatchDow    = false;
		bool matchedMonth  = false;
		bool matchedDayNum = false;
		bool matchedTod    = false;
		bool matchedDow    = false;
		// now make sure all the other dates match that
		for ( k = da ; k < db ; k++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get date #k
			Date *dk = m_dates->m_datePtrs[k];
			// skip empties
			if ( ! dk ) continue;
			// skip if before
			if ( dk->m_a < s2->m_a ) continue;
			// stop if breach
			if ( dk->m_a >= s2->m_b ) break;
			// skip copyright dates, etc.
			if ( dk->m_flags & DF_CLOCK ) continue;
			// skip if fuzzy, like "2"
			if ( dk->m_flags & DF_FUZZY ) continue;
			// no pub dates
			if ( dk->m_flags & DF_PUB_DATE ) continue;
			// make sure it matches the month/daynum of dates
			// that were in the event description
			if ( dk->m_hasType & DT_MONTH ) {
				if ( dk->m_month != monthToMatch )
					noMatchMonth = true;
				else
					matchedMonth = true;
			}
			if ( dk->m_hasType & DT_DAYNUM ) {
				if ( dk->m_dayNum != dayNumToMatch ) 
					noMatchDayNum = true;
				else
					matchedDayNum = true;
			}
			if ( dk->m_hasType & DT_DOW ) {
				if ( dk->m_dow != dowToMatch ) 
					noMatchDow = true;
				else
					matchedDow = true;
			}
			// if s1 had no tod, skip this part. we can't set
			// todMatch and we can't set todNoMatch
			if ( todMin == -1 ) continue;
			// skip if no tod
			if ( ! (dk->m_hasType & DT_TOD ) ) continue;
			// if parent had a range be in that range or stop it
			if      ( todMin >= 0 && dk->m_minTod < todMin ) 
				noMatchTod = true;
			else if ( todMax >= 0 && dk->m_maxTod > todMax ) 
				noMatchTod = true;
			// if parent had a single tod and no range our min
			// must match exactly then
			else if ( todMin >= 0 && 
				  todMax == -1 && 
				  dk->m_minTod != todMin )
				noMatchTod = true;
			else
				// flag it
				matchedTod = true;
		}
		// undo matches?
		if ( noMatchMonth  ) matchedMonth  = false;
		if ( noMatchDayNum ) matchedDayNum = false;
		if ( noMatchTod    ) matchedTod    = false;
		if ( noMatchDow    ) matchedDow    = false;
		// if we did not match all month/daynums of dates in the
		// event description, we failed, and are not a date alias
		if ( noMatchMonth  ) continue;
		if ( noMatchDayNum ) continue;
		// if there was a tod to match we must match it
		if ( noMatchTod ) continue;
		// same for dow
		//if ( dowToMatch >= 0 && ! matchedDow ) continue;

		// . they must had something in common
		// . thegazette.com has just every wednesday in s2 and
		//   some tods. so just matching tods is ok.
		if ( ! matchedMonth && ! matchedDayNum && ! matchedTod ) 
			continue;

		// if they have exactly the same inner tag hashes i would
		// say do not combine either. we are kinda looking for
		// one to have the event title and the other to not have it.
		// so hash all sections inside.
		// this fixes when.com which has two events happening in
		// 9/20 @ 10am at the hispanic cultural center. mainly
		// two different exhibitions. but they have exactly the
		// same inner tag hashes. kinda implying that one is not
		// the title section for the other.
		char dbuf[2000];
		HashTableX dt;
		dt.set(4,0,16,dbuf,2000,false,m_niceness,"bddup");
		long long h1 = 0;
		long long h2 = 0;
		Section *sx = s1;
		for ( ; sx && sx->m_a < s1->m_b; sx = sx->m_next ) {
			QUICKPOLL(m_niceness);
			// avoid repeats
			unsigned long th = sx->m_tagHash;
			if ( dt.isInTable ( &th ) ) continue;
			if ( ! dt.addKey ( &th ) ) return false;
			h1 ^= sx->m_tagHash;
		}
		// clear for this one
		dt.clear();
		sx = s2;
		for ( ; sx && sx->m_a < s2->m_b; sx = sx->m_next ) {
			QUICKPOLL(m_niceness);
			// avoid repeats
			unsigned long th = sx->m_tagHash;
			if ( dt.isInTable ( &th ) ) continue;
			if ( ! dt.addKey ( &th ) ) return false;
			h2 ^= sx->m_tagHash;
		}
		if ( h1 == h2 ) continue;

		// . MERGE THEM!
		// . they must be the same event
		// . swap event ids, essentially merging the two
		sx = s1;
		for ( ; sx && sx->m_a < s1->m_b; sx = sx->m_next ) {
			QUICKPOLL(m_niceness);
			addEventIds ( sx, s2 );
		}
		sx = s2;
		for ( ; sx && sx->m_a < s2->m_b; sx = sx->m_next ) {
			QUICKPOLL(m_niceness);
			addEventIds ( sx, s1 );
		}
	}


	evflags_t mask2 = EV_BAD_EVENT;
	mask2 &= ~ EV_OLD_EVENT;
	Event *lastEvent = NULL;
	///////////////
	//
	// EVENT DESCRIPTION SHARING, part 3
	//
	// inspired by delialahslounge.com!
	// delilahslounge.com had Tues[[]]8pm-10pm as event #1 and
	// basically Tues[[]]8pm as event #2.
	// thus the 2nd date was not able to use the correct title which
	// was monopolized by the first date.
	//
	// . try an event based method
	//
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		//break;
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// . skip it if it got disqualified
		// . now we are much more liberal with "banning" text
		//   from sections we thought to be events...
		if ( ev->m_flags & mask2 ) continue;
		// get prev date
		Event *prev = lastEvent;
		// update it
		lastEvent = ev;
		// need it
		if ( ! prev ) continue;
		// get the dates
		Date *pd = prev->m_date;
		Date *ed =   ev->m_date;
		// do not do it on store hours. TODO: unless something
		// like "No cover until 9pm" on groundkontrol.com, etc.?
		if ( pd->m_flags & DF_STORE_HOURS ) continue;
		if ( ed->m_flags & DF_STORE_HOURS ) continue;
		// must be in agreement
		//if ( ! agree ( ed , pd ) ) continue;
		if ( pd->m_month  != ed->m_month  ) continue;
		if ( pd->m_dayNum != ed->m_dayNum ) continue;
		if ( pd->m_dow    != ed->m_dow    ) continue;
		if ( pd->m_dowBits!= ed->m_dowBits) continue;
		if ( pd->m_minTod >  ed->m_minTod ) continue;
		if ( pd->m_maxTod>=0 && ed->m_minTod>=pd->m_maxTod ) continue;
		if ( pd->m_maxTod>=0 && ed->m_maxTod> pd->m_maxTod ) continue;
		// to fix rateclubs.com, where we are merging first thursday
		// with 2nd thursday event, check the cardinal number
		suppflags_t sfmask = 0;
		sfmask |= SF_FIRST;
		sfmask |= SF_LAST;
		sfmask |= SF_SECOND;
		sfmask |= SF_THIRD;
		sfmask |= SF_FOURTH;
		sfmask |= SF_FIFTH;
		sfmask |= SF_EVERY;
		sfmask |= SF_PLURAL; // nights, mornnings...
		suppflags_t sf1 = pd->m_suppFlags & sfmask;
		suppflags_t sf2 = ed->m_suppFlags & sfmask;
		// every tuesday = tuesdays (fix for delilahslounge.com)
		suppflags_t eq = (SF_PLURAL | SF_EVERY);
		if ( sf1 & eq ) sf1 |= eq;
		if ( sf2 & eq ) sf2 |= eq;
		if ( sf1 != sf2 ) continue;
		// addresses must agree
		if ( ev->m_address->m_hash != prev->m_address->m_hash ) 
			continue;
		// . MERGE THEM!
		// . they must be the same event
		// . swap event ids, essentially merging the two
		for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ){
			// breathe
			QUICKPOLL(m_niceness);
			// one or the other?
			if ( ! si->hasEventId ( prev->m_eventId ) &&
			     ! si->hasEventId (   ev->m_eventId ) )
				continue;
			// add for both now
			si->addEventId ( prev->m_eventId );
			si->addEventId (   ev->m_eventId );
		}
	}


	/*
	////////////////////
	//
	// set Section::m_numTods
	//
	// we use this below for punishing titles that could be titles
	// for multiple SEC_TOD_EVENT sections.
	//
	///////////////////
	for ( long i = 0 ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if none
		if ( ! di ) continue;
		// skip if not tod
		if ( ! ( di->m_hasType & DT_TOD ) ) continue;
		// skip if telescoper
		if ( di->m_hasType & DT_TELESCOPE ) continue;
		// skip if pubdate so trumba.com titles aren't thought to
		// have two tods and be punished so much
		if ( di->m_flags & DF_PUB_DATE ) continue;
		// get section
		Section *sn = sp[di->m_a];
		// blow up to all parents
		for ( ; sn ; sn = sn->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// count it
			sn->m_numTods++;
		}
	}
	*/

	/////////////////////////////
	//
	// set Date::m_mostUniqueDatePtr
	//
	// we use this in the m_numTods algo because we keep telescoping
	// the title sentence section until it contains the section of the
	// m_mostUniquePtr date. before i was trying just to use the tod
	// section but urls like burtstikilounge.com telescoped to the
	// tod in the store hours, so that was causing us to get store hours
	// related titles because they have an m_numTods of 1, while the
	// title we wanted had a high m_numTods, 
	// "Burt's Calendar for the month of November 2009"
	//
	////////////////////////////
	//
	// so scan all events and take the dates from them. and also
	// do scan events that have "oldtimes" set, too!
	evflags_t mask = EV_BAD_EVENT;
	mask &= ~ EV_OLD_EVENT;
	// scan the events
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// . skip it if it got disqualified
		// . now we are much more liberal with "banning" text
		//   from sections we thought to be events...
		if ( ev->m_flags & mask ) continue;
		// get the date
		Date *ed = ev->m_date;
		// hash each ptr to count their usage
		for ( long i = 0 ; i < ed->m_numPtrs ; i++ ) 
			// inc its count
			ed->m_ptrs[i]->m_usedCount++;
	}
	// now do scan again and set the Date::m_eventsUsed count of it
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// . skip it if it got disqualified
		// . now we are much more liberal with "banning" text
		//   from sections we thought to be events...
		if ( ev->m_flags & mask ) continue;
		// get the date
		Date *ed = ev->m_date;
		// timestamps have no ptrs
		if ( ed->m_hasType == (DT_TIMESTAMP|DT_COMPOUND) ) {
			ed->m_mostUniqueDatePtr = ed;
			continue;
		}
		// init these
		Date *best = NULL;
		long  min  = 999999;
		// hash each ptr to count their usage
		for ( long i = 0 ; i < ed->m_numPtrs ; i++ ) {
			// . let's try marking all in the telescope
			// . mult events penalty will penalize the headers
			Section *ds;
			ds = ed->m_ptrs[i]->m_section->m_sentenceSection;
			ds->m_sentFlags |= SENT_HASSOMEEVENTSDATE;
			// get lowest
			if ( ed->m_ptrs[i]->m_usedCount >= min ) continue;
			// set it
			min  = ed->m_ptrs[i]->m_usedCount;
			best = ed->m_ptrs[i];
		}
		// set the most unique date ptr then in this date
		ed->m_mostUniqueDatePtr = best;
		// get section
		//Section *ss = best->m_section->m_sentenceSection;
		// set flag
		//ss->m_sentFlags |= SENT_HASSOMEEVENTSDATE;
	}




	/////////////////////////
	//
	// set EV_SENTSPANSMULTEVENTS
	//
	// fixes mercury.intouch-usa.com which has a sentence that
	// has the tod of one event spanning into the other section
	// that contains the title of the adjacent event simply because
	// of inadequate taggage to separate the events.
	// specifically eventids 103 and 104 share a sentence!
	// the formatting of mercury.intouch-usa.com is extremely horrible,
	// just look at it and you will get the wrong dates yourself!!
	//
	/////////////////////////
	bool toxic = false;
	for ( Section *si = ss->m_firstSent; si; si=si->m_nextSent ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not for us
		if ( si->m_minEventId <= 0  ) continue;
		// is it a split sentence?
		if ( ! ( si->m_flags & SEC_SPLIT_SENT ) ) continue;
		// must be a tod date in it otherwise hurts
		// jccrochester.org and oppenheimer which had splits in the
		// header sections. i just don't want one event using the tod 
		// of another event just because its in a shared sentence, 
		// so-to-speak.
		if ( ! si->m_todXor ) continue;
		// get first section
		Section *sp1 = sp[si->m_senta];
		// and last
		Section *sp2 = sp[si->m_sentb-1];
		// hopefully then sp1 and sp2 are not sentence sections
		if ( sp1->m_minEventId == sp2->m_minEventId &&
		     sp1->m_maxEventId == sp2->m_maxEventId ) 
			continue;
		// ok, we got a problem, jeckyl and hyde
		// any event that has the "si" sentence, mark as bad to
		// be on the safe side
		for ( long e = 0 ; e < m_numEvents ; e++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get event
			Event *ev = &m_events[e];
			// contained
			if ( ! si->hasEventId(ev->m_eventId) ) continue;
			// flag it
			ev->m_flags |= EV_SENTSPANSMULTEVENTS;
			// now if we do this for any events mark them all
			// to avoid the condition of the top most sentence
			// pairing up with the bottom most date in a td cell
			// mercury.intouch-usa.com.
			toxic = true;
		}
	}	
	// if the whole thing is toxic, mark them all
	for ( long e = 0 ; toxic && e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// flag it
		ev->m_flags |= EV_SENTSPANSMULTEVENTS;
	}


	////////////////////////
	//
	// set SENT_MULT_EVENTS
	//
	////////////////////////
	for ( Section *si = ss->m_rootSection ; si ; si = si->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// now we require the sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if not in description right now, no need to score it!
		if ( si->m_minEventId <= 0 ) continue;
		// if description used by multiple events it is not as good
		// as a title candidate. we want to try to find the most
		// unique title for each event.
		// crap! this algo does not work well for blackbirdbuvette.com
		// because its hours are all separated by soft sections
		//long ned = si->m_maxEventId - si->m_minEventId;
		//long ned = si->m_numEventIdBits;
		// get first event section
		Event *e1 = m_idToEvent[si->m_minEventId];
		Event *e2 = m_idToEvent[si->m_maxEventId];
		// sanity check
		if ( si->m_maxEventId >= MAX_EVENTS+1) { char *xx=NULL;*xx=0;}

		/*
		Section *s1 = sp[e1->m_date->m_mostUniqueDatePtr->m_a];
		Section *s2 = sp[e2->m_date->m_mostUniqueDatePtr->m_a];
		// telescope s1 until contains s2 or hits hard section
		for ( ; s1 ; s1 = s1->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// contains s2?
			if ( s1->contains ( s2 ) ) break;
			// or if hard section
			if ( m_sections->isHardSection(s1) ) break;
		}
		*/

		// don't apply the multevents title penalty if the two events
		// basically have their dates adjacent to each other... or if 
		// the two events wouldn't otherwise separate the dates with a
		// title from one. i.e. if there is some text from one event 
		// that separates the dates and is a good title candidate 
		// (i.e. capitalized, etc.) then we can apply the mult events
		// penalty... otherwise iw ouldn't apply it. shoudl fix 
		// santafeplayhouse and the turkey run page, so they get good 
		// titles. in essence, this is "sameEvent" below, so we
		// get the event id bits in the section, si, and check
		// out each date in there. then if all dates are basically
		// next to each other, set sameEvent to true... or just
		// scan from e1->m_date->m_a to e2->m_date->m_a and if
		// there is nothing "disruptive" in between, assume basically
		// different times for the same event... or just set "ned"
		// to the # of truly different event dates i guess...

		// only set if does not contain
		if ( si->m_numEventIdBits > 1 )
			si->m_sentFlags |= SENT_MULT_EVENTS;

		/*
		bool sameEvent = false;//(s1->contains(s2));
		long firstTime = 0;
		for ( ; ! sameEvent && ned > 1 ; ned-- , firstTime++ ) {
			if ( firstTime == 0 )
				si->m_sentFlags |= SENT_MULT_EVENTS;
			else if ( firstTime == 1 )
				si->m_sentFlags |= SENT_MULT_EVENTS_3;
			else if ( firstTime == 2 )
				si->m_sentFlags |= SENT_MULT_EVENTS_4;
			else if ( firstTime == 3 )
				si->m_sentFlags |= SENT_MULT_EVENTS_5;
			else if ( firstTime == 4 )
				si->m_sentFlags |= SENT_MULT_EVENTS_6;
			else if ( firstTime == 5 )
				si->m_sentFlags |= SENT_MULT_EVENTS_7;
			else
				break;
			// . only do once now
			// . cuz was getting titlescore of 0 for salsapower.com
			//break;
		}
		*/

		// 
		// this section even if it only describes one event might
		// reference a bunch of invalid events, so also go by the
		// the SEC_TOD_EVENT tag too!
		//
		// the idea is to punish titles that are in a different
		// SEC_TOD_EVENT section than the date!

		// reset s1
		//s1 = sp[e1->m_date->m_a];
		// and get the section with the descriptive text as "sn"
		//Section *sn = si;//s1;//si; mdw mdw
		//Date *d2 = e2->m_date;
		// get section around most unique date ptrs of event date
		Date *mu1 = e1->m_date->m_mostUniqueDatePtr;
		Date *mu2 = e2->m_date->m_mostUniqueDatePtr;
		// must be there
		if ( ! mu1 ) { char *xx=NULL;*xx=0; }
		if ( ! mu2 ) { char *xx=NULL;*xx=0; }
		// get section around it
		Section *sn = sp[mu1->m_a];
		// only do if not mult events already
		if ( si->m_sentFlags & SENT_MULT_EVENTS ) sn = NULL;
		// now expand s1
		for ( ; sn ; sn = sn->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . stop if we contain the event's date
			// . ANY of the telescoped to dates
			//long i; for ( i = 0 ; i < d1->m_numPtrs ; i++ ) {
			//	Date *di = d1->m_ptrs[i];
			//	Section *ds = sp[di->m_a];
			//	if ( sn->contains(ds) ) break; // s1
			//}
			//if ( i < d1->m_numPtrs ) break;
			// stop when this event contains the description
			if ( sn->contains(si) ) break;
			/* mdw mdw
			if ( sn->m_numTods >= 2 ) {
				// flag it
				tflags |= SENT_MULT_EVENTS;
				// . penalize it!
				// . for a lot of sites we punished so much
				//   because a header would have like 6+ tods
				//   under it that we ended up taking lower
				//   case titles over such headers, so back
				//   of to .98 after the first hit!
				for ( long k = 1;k<sn->m_numTods;k++) {
					if ( k == 1 ) tscore *= .25;
					else          tscore *= .85;
				}
			}
			// stop if we hit a section containing a tod
			//if ( sn->m_numTods > 0 ) break;
			// . no, only stop when it contains our tod date
			// . this fixes burtstikilounge.com which
			//   get the address of burts lounge as the title
			//   of each event because it is in the store hours
			//   sections which only has one tod
			if ( sn->contains ( dsec1 ) && 
			     sn->contains ( dsec2 ) )
				break;
			*/
			
			// stop once we hit a section that is or contains
			// an SEC_TOD_EVENT section and is not a heading
			// or ex-heading section
			//if ( sn->m_numImpliedEvents > 0 &&
			//     !(sn->m_flags & SEC_HEADING_CONTAINER) &&
			//     !(sn->m_flags & SEC_NIXED_HEADING_CONTAINER) )
			//	break;
			//if ( sn->m_flags & SEC_TOD_EVENT   ) break;
			// check this
			//if ( ! ( sn->m_flags & SEC_TOD_EVENT ) ) continue;
			// . try using event brothers logic now
			// . should help with santafeplayhouse since its
			//   dates are all in a little list and should not have
			//   SEC_EVENT_BROTHER_SET for them (TODO) thus we
			//   will not think of them as multiple events...
			if ( ! ( sn->m_flags & SEC_EVENT_BROTHER ) ) continue;
			// penalize it!
			//tscore *= .25;
			//dscore *= .25;
			si->m_sentFlags |= SENT_MULT_EVENTS;
			break;
		}

		// after tod event title penalty
		//if ( si->m_a > e1->m_date->m_a ) {
		//	tscore *= .80;
		//	tflags |= SENT_AFTER_DATE;
		//}

		// . combination penalties
		// . this fixed something i think, but is hurting the
		//   title we want for newmexico.org
		//if ( ( tflags & SENT_DUP_SECTION ) &&
		//     ( tflags & SENT_PAGE_REPEAT ) )
		//	tscore *= .40;

		Date *d1 = e1->m_date;
		// reset
		sn = si;
		// telescope up, if hits an <li> tag before containing
		// our event, then punish!
		for ( ; sn ; sn = sn->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if we contain the event's date
			//if ( sn->contains(s1) ) break;
			// . stop if we contain the event's date
			// . ANY of the telescoped to dates
			long i;	for ( i = 0 ; i < d1->m_numPtrs ; i++ ) {
				Date *di = d1->m_ptrs[i];
				Section *ds = sp[di->m_a];
				if ( sn->contains(ds) ) break; // s1
			}
			if ( i < d1->m_numPtrs ) break;
			// punish
			if ( sn->m_tagId != TAG_LI ) continue;
			si->m_sentFlags |= SENT_IN_LIST;
			// count brothers, if we got 4+ consider it
			// a big list
			long bcount = 0;
			Section *bs = sn->m_prevBrother;
			for ( ; bs ; bs = bs->m_prevBrother ) {
				QUICKPOLL(m_niceness);
				if ( ++bcount >= 3 ) break;
			}
			// go forward as well
			bs = sn->m_nextBrother;
			for ( ; bs ; bs = bs->m_nextBrother ) {
				QUICKPOLL(m_niceness);
				if ( ++bcount >= 3 ) break;
			}
			// a big list?
			if ( bcount >= 3 ) si->m_sentFlags |= SENT_IN_BIG_LIST;
			break;
		}			
	}

	//////////////////////////
	//
	// TODO: set SENT_GENERIC_WORDS here and use SENT_GENERIC_TITLE for
	// setEventTitle().
	//
	/////////////////////////


	//
	// TODO: move this block below into setEventTitle() function and
	// set sflags not the actual sentence flags.
	//

	//////////////////////////
	//
	// PUNISH if after date and full sentence
	//
	// typically the title occurs BEFORE the sentence description
	// of the event. the problem is is that we might mistake a menu-y
	// sentence for an event description sentence. also, we do not
	// punish if a full sentence does occur after the title candidate?
	//
	// PROBLEM: for salsapower.com two events that are in the same
	// basic section, one sets SENT_AFTER_SENTENCE on the title of the
	// other. so this is an argument for keeping event title scores
	// separated somehow by eventid, perhaps in a hashtable? 
	// BUT i think this is really a virtual sections issue. we need
	// a virtual section to partition those two events.
	//
	/////////////////////////
	//
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get the date's section (TOD's section)
		Section *si = ev->m_date->m_section;
		// should be there!
		if ( ! si ) continue;
		// if there must be sentence section... hmmm sometimes it
		// is not! wtf?
		//if(si && ! (si->m_flags&SEC_SENTENCE)){char *xx=NULL;*xx=0;}
		// . skip over the section containing the event tod
		// . do not count the date section as the sentence
		long mina = si->m_b;
		for ( ; si && si->m_a < mina ; si = si->m_next );
		// scan sections down from there
		for ( ; si ; si = si->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not sentence
			if ( !(si->m_flags & SEC_SENTENCE) ) continue;
			// skip if not part of this event id
			if ( ! si->hasEventId(ev->m_eventId) ) continue;
			// skip if not ending in period
			if ( !(si->m_sentFlags & SENT_PERIOD_ENDS) ) continue;
			// skip if generic (not yet supported!)
			//if ( si->m_sentFlags & SENT_GENERIC_WORDS ) continue;
			// ok, penalize all title candidates after this
			// belonging to this event. but skip the section that 
			// is the sentence, might be title!
			si = si->m_next;
			break;
		}
		// penalize loop
		for ( ; si ; si = si->m_next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not sentence
			if ( !(si->m_flags & SEC_SENTENCE) ) continue;
			// skip if not part of this event id
			if ( ! si->hasEventId(ev->m_eventId) ) continue;
			// skip if already docked!
			if ( si->m_sentFlags & SENT_AFTER_SENTENCE ) continue;
			// dock it, minimal impact
			//si->m_titleScore *= .05; // 99;
			si->m_sentFlags |= SENT_AFTER_SENTENCE;
		}
	}

	//
	// need to set m_dateHash and m_addressHash now so that
	// XmlDoc::getTurkVotingTable() can use it before it calls
	// Events::setTitles()
	//
	//
	// set Event::m_dateHash64, addressHash64, dateTagHash32, addTagHash32
	//
	//////////////////
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// get id
		long eid = ev->m_eventId;
		// skip if not valid
		if ( eid <= 0 ) continue;
		// ignore if over 255
		if ( eid > 255 ) continue;
		// skip if bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// first set title hash, etc.
		ev->m_dateHash64    = (uint64_t)ev->m_date->m_dateHash64;
		ev->m_addressHash64 = ev->m_address->m_hash;

		// let PageEvents.cpp know the Address::m_street is
		// a lat/lon address...
		if ( ev->m_address->m_flags3 & AF2_LATLON )
			ev->m_flags |= EV_LATLONADDRESS;

		///////////////////
		//
		// set Event::m_dateTagHash,m_addressTagHash
		//
		//////////////////

		// get most unqiue component of the dates
		//Date *ud = ev->m_date->m_mostUniqueDatePtr;
		// sanity
		//if ( ud->m_a < 0 ) { char *xx=NULL;*xx=0; }
		// get his section's tag hash then
		//ev->m_dateTagHash32 = ud->m_turkTagHash32;
		ev->m_dateTagHash32 = ev->m_date->m_dateTypeAndTagHash32;
		// sanity check
		if ( ev->m_dateTagHash32 == 0 ) { char *xx=NULL;*xx=0; }
			

		//sp[ud->m_a]->m_sentenceSection->m_tagHash;

		// . address street (might be "fake" street)
		// . use origPlace since ev->m_address might be an alias
		Place *street = ev->m_origPlace;
		//Place *street = ev->m_address->m_street;
		// if not in doc, try name
		if ( street && street->m_a < 0 ) street=ev->m_address->m_name1;
		// still not? error!
		if ( street && street->m_a < 0 ) { char *xx=NULL;*xx=0; }
		// . use section then
		Section *es = NULL;
		if ( street ) es = sp[street->m_a]->m_sentenceSection;
		// the lat/lon might NOT be in a sentence. often it is in a lnk
		if ( es ) 
			ev->m_addressTagHash32 =
				sp[street->m_a]->m_sentenceSection->
				m_turkTagHash;
		else
			// this might core down the line!
			ev->m_addressTagHash32 = 0;

		//
		// composite hashes
		//
		unsigned long h1;
		unsigned long h2;
		// do tag hash composite
		h1 = (unsigned long)ev->m_addressTagHash32;
		h2 = (unsigned long)ev->m_dateTagHash32;
		ev->m_adth32 = hash32h( h1 , h2 );

		// PROBLEM: some events on the same page have the same
		// adch32 but are really different events. like for
		// pacificmedicalcenter.org which has the optical shop
		// hours and the lab hours being the same and this having
		// the same adch32. 
		// SOLUTION: identify a unique sentence within the topological
		// radius of only that event's most unique date ptr. then
		// incorporate that sentence's hash into the adch32.
		// TODO: modify adch32 accordingly and display the ush32
		// (Unique Sentence Hash 32) in the event row like we already
		// do adch32 and adth32, etc.

		//
		// HACK:
		// 
		// when placedb uses the place name it changes the tag hash
		// on us!!! so when the page is reindexed the turk votes
		// do not match any event because they were using the adth32
		// corresponding to the old tag hash. do a qa.html run
		// with salsapower.com and then do a page parser on it after
		// the run to see how the event hashes change. double time
		// dance studio is used for the address tag hash then...
		// as opposed to the street address itself...
		// mdw update 9/19/11:
		// we need this in order to reject event date formats based
		// on the address tag hash, like when gigablast is selecting
		// the wrong address but the right date, we have to be able
		// to vote on that and not just kill all events with that
		// turk tag hash for the date.
		//ev->m_adth32 = (unsigned long)ev->m_dateTagHash32;

		// content hashes now
		h1 = (unsigned long)ev->m_addressHash64;
		h2 = (unsigned long)ev->m_dateHash64;
		ev->m_adch32 = hash32h( h1 , h2 );
	}

	return true;
}

// . this is called by XmlDoc::getTurkedEvents() after calling Events::set() 
// . HACK: we alter m_eventSentFlagsTable in order to add the
//         EVSENT_IS_INDEXABLE flags...
// . est = event sent flags table
bool Events::setTitlesAndVenueNames ( HashTableX *tbt , HashTableX *evsft ) {

	Sections   *ss   = m_sections;
	Section   **sp   = ss->m_sectionPtrs;

	m_tbt = tbt;
	// save this for use while hashing, etc.
	m_evsft = evsft;

	// pre alloc one slot per sentence/event
	long need = m_numEvents * ss->m_numSentenceSections * 3;
	// scores are floats
	if ( ! m_titleScoreTable.set(8,4,need,NULL,0,false,m_niceness,"titsc"))
		return false;
	if ( ! m_descScoreTable .set(8,4,need,NULL,0,false,m_niceness,"dessc"))
		return false;

	// now compute the title info and descScore for each event
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Event *ev = &m_events[i];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// . set ev->m_bestVenue ptr to a Place name
		// . call this before setTitle() since it will set
		//   SENT_HASEVENTADDRESS on the sentence that contains
		//   Event::m_bestVenueName
		if ( ! setBestVenueName ( ev ) ) return false;
		// set ev->m_titleStart/m_titleEnd/m_titleSec/m_titleScore
		// and also sets each section's m_descScore
		if ( ! setTitle ( ev )) return false;
		// no title flag?
		if ( ! ev->m_titleSection ) {
			ev->m_flags |= EV_HADNOTITLE;
			log("events: had no title for %s",m_url->m_url);
		}
	}

	// set this for printing out in the validator
	//m_revisedValid = m_numValidEvents;

	// links class must be valid for setting this!
	if ( ! m_links ) { char *xx=NULL;*xx=0; }

	/////////////////////////
	//
	// set EV_STORE_HOURS flag
	//
	/////////////////////////
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// index events with strong titles we index gbeventindicator:1
		//if ( ev->m_titleSection->m_sentFlags & SENT_HASTITLEWORDS )
		//	ev->m_flags |= EV_HASTITLEINDICATOR;
		// or if pre-word was like "title:"
		//if ( ev->m_titleSection->m_sentFlags & SENT_TITLE_INDICATED)
		//	ev->m_flags |= EV_HASTITLEINDICATOR;
		// if event is a subsentence, consider indicated as well!
		//if ( isSubsent )
		//	ev->m_flags |= EV_HASTITLEINDICATOR;
		// this overrides all though
		bool storeHours = (ev->m_date->m_flags & DF_STORE_HOURS);
		// get title content hash
		unsigned long tch32 = 0;
		if ( ev->m_titleSection ) 
			tch32 = ev->m_titleSection->m_sentenceContentHash;
		// the venue name hash
		unsigned long vch32 = 0;
		if ( ev->m_bestVenueName ) 
			vch32 = ev->m_bestVenueName->m_simpleHash32;
		// also say yes if title equals venue name
		if ( tch32 && tch32 == vch32 &&
		     // . not if title section is an event beginning though
		     // . fixes "9th annual...\naddress" for trumba.com
		     //   because SENT_PLACE_NAME is set for it even though
		     //   it is the event title, not the placename, just
		     //   because it is above the street intersection.
		     !(ev->m_titleSection->m_sentFlags &SENT_HASTITLEWORDS) )
		     // or indicating event by the first word ("ceremony",etc.)
		     //!(ev->m_titleSection->m_sentFlags &SENT_EVENT_ENDING))
			storeHours = true;
		// UNLESS we are on a specific day!!!
		// fixes sybarite.org which ends up using the place name
		// "Robertsons Recital Hall" as the title for an event only
		// because there are no other titles!!
		if ( (ev->m_date->m_hasType & DT_DAYNUM) &&
		     // NOT a range, must be single day!!!
		     // guysndolls: August 20th [[]] 4:00 p.m. until 10:00...
		     ev->m_date->m_minDayNum == ev->m_date->m_maxDayNum )
			storeHours = false;
		// a range of daynums is not store hours IFF we do not
		// associate the daynum range with a season, like "Fall Hours".
		// we need to make sure utexas.edu library which has 
		// "Fall Hours" and the daynum range of fall, remains 
		// a store hours date
		if ( (ev->m_date->m_hasType & DT_DAYNUM) &&
		     !(ev->m_date->m_hasType & DT_SEASON) )
			storeHours = false;
		if ( storeHours ) ev->m_flags |= EV_STORE_HOURS;
		// and this now too, for dates that telescope to the store 
		// hours and have no specific daynum(s)
		if ( ev->m_date->m_flags & DF_SUBSTORE_HOURS )
			ev->m_flags |= EV_SUBSTORE_HOURS;
	}

	///////////////////////////////////////////
	//
	// Set EV_OUTLINKED_TITLE flags
	//
	// . these events most often have their own web page
	//
	///////////////////////////////////////////	
	// 
	// we no longer have this flag in EV_BAD because if a turk forces
	// the title to an outlinked title the event was disappearing and
	// we lost the turks voting record for it.
	//
	Words      *ww   = m_words;
	nodeid_t   *tids = ww->getTagIds();
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// get section
		//Section *si = ev->m_section;
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// title start
		long ta = ev->m_titleStart;
		// sanity check
		if ( ta < 0 ) continue;
		// if in SEC_NOT_MENU, we are ok too!
		Section *ts = m_sections->m_sectionPtrs[ta];
		// . if title not in link, skip it
		// . not quite, could be in a sentence section first!
		//if ( ! ( sp->m_flags & SEC_A ) ) continue;
		// . if title section is not menu, skip it
		// . this hurts the 2nd newmexico.org url because by chance
		//   the event menu is different
		// . but not having this hurts mrmovietimes.com whose outlinked
		//   titles lead to only a movie description...
		//if ( ts->m_flags & SEC_NOT_MENU ) continue;
		// . if it is a buy tickets section, skip it!
		// . fixes title "Buy tickets from $54" for
		//   events.mapchannels.com/Index.aspx?venue=628
		//if ( rt->isInTable ( &ts ) ) continue;
		if ( ts->m_flags & SEC_HAS_REGISTRATION ) continue;
		// do not scan back out of event section
		long tamin = ts->m_a;
		// if no title, bail
		if ( ta < tamin ) continue;
		// . allow address through
		// . fixes unm.edu/~willow/.... which had a place name
		//   in one of its event titles which was outlinked
		// . also fixed booktour.com/author/15475 for same reason
		if ( m_bits[ta] & D_IS_IN_ADDRESS ) continue;
		if ( m_bits[ta] & D_IS_IN_VERIFIED_ADDRESS_NAME ) continue;
		// allow it to go back a bit more in case we are in an
		// SEC_SENTENCE section and the anchor tag is just outside
		tamin -= 30; if ( tamin < 0 ) tamin = 0;
		// get the hyperlink its in
		for ( ; ta >= tamin ; ta-- ) 
			// stop on </a> or <a ...>
			if ( (tids[ta] & BACKBITCOMP) == TAG_A ) break;
		// if no href tag bail
		if ( ta < tamin ) continue;
		// grab the node number in xml.cpp from the word #
		long nn = ww->m_nodes[ta];
		// skip if self link
		if ( m_xml->m_nodes[nn].m_isSelfLink ) continue;
		// must be <a href> not <a name>
		long len; 
		char *link = m_xml->getString(nn,"href",&len);
		if ( ! link ) continue;
		// . must not be "mailto:"
		// . fixes www.cabq.gov/library/branches.html
		if ( len >= 7 && ! strncasecmp(link,"mailto:",7) ) continue;

		//
		// BUT if the date AND title are in a section that CONTAINS
		// the address/street, then it is OK! the whole purpose of
		// this algo is to fix the newmexico.org urls and others
		// that have a list of events that are outlinked and
		// do not correspond to the address on the page. 
		//
		Section *s1 = ev->m_date->m_section;
		Section *s2 = ts;
		// find smallest section containing the date AND title
		Section *sn = s1;
		for ( ; sn ; sn = sn->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// grow until we contain the title section
			if ( ! sn->contains ( s2 ) ) continue;
			break;
		}
		// . ok, now sn contains the date and the title, if that
		//   also contains the address, assume we are self contained
		// . use the original "street" address. could be a "fake"
		//   street in that it is a name
		//Section *as = ev->m_address->m_section;
		Section *as = NULL;
		// if address is "venueDefault" then ev->m_origPlace is NULL, 
		// and its not in a section anyway, so watch out...
		if ( ev->m_origPlace && ev->m_origPlace->m_a >= 0 ) 
			as = sp[ev->m_origPlace->m_a];
		if ( as && sn && sn->contains ( as ) ) 
			continue;

		// . mark it
		// . this isn't part of the "bad flags" because we already
		//   set Section::m_min/maxEventId above, so just indicate
		//   that the title is outlinked when printing out
		ev->m_flags |= EV_OUTLINKED_TITLE;
		// the total count
		//m_revisedValid--;
	}

	//
	//
	// EventSentFlags part TWO !!!!!!!
	//
	//
	// . SET EVSENT_IS_INDEXABLE flag for each event/sentence pair
	// . we could not do this above because we did not have titles!
	//
	//
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get event id
		long eid = ev->m_eventId;
		// scan each sentence
		for ( Section *si = ss->m_firstSent; si; si=si->m_nextSent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not for us
			if ( si->m_minEventId <= 0  ) continue;
			if ( si->m_minEventId > eid ) continue;
			if ( si->m_maxEventId < eid ) continue;
			// must be us
			if ( ! si->hasEventId ( ev->m_eventId ) ) continue;
			// skip if not indexable
			if ( ! isIndexable ( ev , si ) ) continue;
			// mix it up!
			uint32_t evkey = hash32h((uint32_t)ev,12345);
			// make the key
			uint64_t key = (((uint64_t)si)<<32) | (uint32_t)evkey;
			// shortcut
			//HashTableX *ht = &m_eventSentFlagsTable;
			// add the sentence flags for this event
			esflags_t *esp = (esflags_t *) m_evsft->getValue(&key);
			// if there, add in
			if ( esp ) { *esp |= EVSENT_IS_INDEXABLE; continue; }
			// make flags
			esflags_t tmp = EVSENT_IS_INDEXABLE;
			// otherwise, add anew
			if ( ! m_evsft->addKey ( &key , &tmp ) ) return false;
		}
	}

	///////////////
	//
	// set EVSENT_ADDRESS_ONLY if just an address plus generic words
	//
	///////////////

	

	//
	// SPECIAL DEDUPING
	//
	// events like those on http://www.reverbnation.com/venue/448772
	// have the same date twice, and with telescoping we get it thrice
	// for the same event. so remove such dups. if same times and
	// same description/title...
	//
	HashTableX dt;
	char dtbuf[1024];
	dt.set ( 8 ,0 ,64 , dtbuf, 1024 , false , m_niceness ,"evdt2");
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// reset this
		ev->m_numDescriptions = 0;
		// get event id
		long eid = ev->m_eventId;
		// get hash
		long long h = 0LL;
		// scan all sections and combine their hashes to get our
		// title/description hash, but skip date sections
		for ( Section *si = ss->m_firstSent; si ; si = si->m_nextSent){
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not for us
			if ( si->m_minEventId <= 0  ) continue;
			if ( si->m_minEventId > eid ) continue;
			if ( si->m_maxEventId < eid ) continue;
			// must be us
			if ( ! si->hasEventId ( ev->m_eventId ) ) continue;
			// skip if desc score is 0 because it is in a menu
			// i guess... but we should make this score positive
			// if it gets selected as part of the title???
			//if ( si->m_descScore == 0.0 &&
			//     // allow if title though for this event
			//     ev->m_titleSection != si ) continue;
			// skip if date only, that can differ slightly
			if ( si->m_sentFlags & SENT_IS_DATE      ) continue;
			if ( si->m_sentFlags & SENT_NUMBERS_ONLY ) continue;
			// skip if not indexable
			esflags_t esflags = getEventSentFlags (ev,si,m_evsft );
			if ( ! (esflags & EVSENT_IS_INDEXABLE ) ) continue;
			// combine hash
			h <<= 1LL;
			h ^= si->m_contentHash;
			// skip if shared by multiple events
			if ( si->m_sentFlags & SENT_MULT_EVENTS ) continue;
			// skip if date only
			long numAlnumWords = si->m_alnumPosB - si->m_alnumPosA;
			// count # of descriptions for use below
			ev->m_numDescriptions += numAlnumWords;
		}
		// . now i just use adch32
		// . i hash the intervals like this because m_dateHash64 is
		//   too unreliable because it can be different for a differnt
		//   time!
		//h = ev->m_address->m_hash;
		// then hash all the event time intervals
		Interval*iv=(Interval*)(ev->m_intervalsOff+m_sb.getBufStart());
		long long eh =hash64((char *)iv,ev->m_ni*sizeof(Interval),0LL);
		// xor it in
		//h ^= eh;
		h = hash64 ( eh , h );
		// if dup, mark it
		if ( dt.isInTable ( &h ) ) {
			// flag it
			ev->m_flags |= EV_SPECIAL_DUP;
			// the total count
			//m_revisedValid--;
			continue;
		}
		// add it
		if ( ! dt.addTerm ( &h ) )
			return false;
	}

	/////////////////////
	//
	// set EV_ADCH32DUP
	//
	/////////////////////

	// 
	// make sure adch32 is unique on page. that is basically how
	// we identify an event for turking purposes.
	//
	char adtb[1024];
	HashTableX adchtab;
	adchtab.set( 4,4,64,adtb,1024,false,m_niceness,"adtbtab");
	
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// get id
		long eid = ev->m_eventId;
		// skip if not valid
		if ( eid <= 0 ) continue;
		// ignore if over 255
		if ( eid > 255 ) continue;
		// skip if bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// shortcut
		long key = ev->m_adch32;
		// is dup?
		Event **dupp = (Event **)adchtab.getValue ( &key );
		// add it if not in there.
		if ( ! dupp ) {
			// . put us in there if nobody was there
			// . return false with g_errno set on error
			if ( ! adchtab.addKey ( &key, &ev ) ) return false;
			continue;
		}
		// get the guy we're a dup of
		Event *dup = *dupp;
		// prefer the one that is not store hours
		if ( (ev->m_flags & EV_STORE_HOURS) &&
		     !(dup->m_flags & EV_STORE_HOURS) ) {
			// we are the dup then
			ev->m_flags |= EV_ADCH32DUP;
			continue;
		}
		// or maybe he is the dup
		if ( (dup->m_flags & EV_STORE_HOURS) &&
		     !(ev->m_flags & EV_STORE_HOURS) ) {
			// we are the dup then
			dup->m_flags |= EV_ADCH32DUP;
			// we take his place in the table
			*dupp = ev;
			continue;
		}

		// prefer the one with the most descriptions
		if ( dup->m_numDescriptions >= ev->m_numDescriptions ) {
			// we are the dup then
			ev->m_flags |= EV_ADCH32DUP;
			continue;
		}
		// otherwise, he is the dup...
		dup->m_flags |= EV_ADCH32DUP;
		// we take his place in the table
		*dupp = ev;
	}


	// . no no, if the place name is obvious it should be in our
	//   table of Places already! so we should cover it in that!!!
	/*
	/////////////////////////////
	//
	// set EV_AMBIGUOUS_LOCATION
	//
	// - if event description contains an obvious place name AND
	//   its primary address is on root page and the obvious place name
	//   is not, then assume the obvious place name is the proper one
	//
	/////////////////////////////
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get address
		Address *aa = ev->m_address;
		// get section of street
		long stra = aa->m_street->m_a;
		// skip if street no from page. probably from tagrec set
		// by the contact info page
		if ( stra < 0 ) continue;
		// get section of street
		Section *sx = sp[stra];
		// get sentence
		for ( ; sx ; sx = sx->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if sentence
			if ( sx->m_flags & SEC_SENTENCE ) break;
		}
		// stop if not sentence... wtf?
		if ( ! sx ) continue;
		// if street is not on root page, event is ok
		//if ( ! (sx->m_sentFlags & SENT_ONROOTPAGE) ) continue;
		if ( sx->m_votesForNotDup > sx->m_votesForDup ) continue;
		// scan the description to see if has an obvious place name
		Section *si; for ( si = ss->m_rootSection ;si; si=si->m_next){
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not sentence
			if ( !(si->m_flags & SEC_SENTENCE) ) continue;
			// skip if not part of this event id
			if ( ! si->hasEventId(ev->m_eventId) ) continue;
			// . if obvious place on root page too, forget it!
			// . fixes woodencowgallery.com walk on wild side url
			//   which has the gallery name in the copyright below
			//   in addition to the street address near the
			//   store hours
			//if ( si->m_sentFlags & SENT_ONROOTPAGE ) continue;
			if ( si->m_votesForDup > si->m_votesForNotDup)continue;
			// stop if we hit an obvious place
			if ( si->m_sentFlags & SENT_OBVIOUS_PLACE ) break;
		}
		// we're ok if no obvious place name section in desc.
		if ( ! si ) continue;
		// otherwise, nuke it
		ev->m_flags |= EV_AMBIGUOUS_LOCATION;
		// the total count
		//m_revisedValid--;
	}
	*/

	/*

i tried to fix wrong address for stoart.com, but what if ultiamately he
does not even have his gallery address on this page, just the false one?

	/////////////////////////////
	//
	// set EV_AMBIGUOUS_LOCATION if sandwiching addresses
	//
	/////////////////////////////
	for ( long e = 0 ; e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// get the date
		Date *di = ev->m_date;
		// skip if none
		if ( ! di ) continue;
		// panic if no ptrs
		if ( di->m_numPtrs == 0 ) { char *xx=NULL;*xx=0; }
		// reset it
		long toda = -1;
		// scan all dates in telescope
		for ( long j = 0 ; j < di->m_numPtrs ; j++ ) {
			// get it
			if ( ! ( di->m_ptrs[j]->m_hasType & DT_TOD )) continue;
			// set it
			toda = di->m_ptrs[j]->m_a;
			// stop
			break;
		}
		if ( toda < 0 ) { char *xx=NULL;*xx=0; }
		// set before and after
		Address *before = NULL;
		Address *after  = NULL;
		// get # of places
		long np = m_addresses->m_ns;
		// get address before and below
		for ( long i = 0 ; i < np ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// get the place
			Place *p = &m_addresses->m_streets[i];
			// get the address containing the street
			Address *addr2 = p->m_address;
			// use alias?
			if ( ! addr2 && p->m_alias ) addr2 = p->m_alias;
			// skip if empty still
			if ( ! addr2 ) continue;
			// is it before?
			if ( p->m_a < toda ) {
				before = addr2;
				continue;
			}
			if ( p->m_a > toda ) {
				after = addr2;
				break;
			}
		}
		// sanity check. no, because address we use could be a
		// fake street name address, and the address after toda
		// 
		//if ( ev->m_address != before && ev->m_address != after ) { 
		//	char *xx=NULL;*xx=0;}

		// cancel out poboxes or ticket places
		pflags_t mask = 0;
		mask |= PLF2_IS_POBOX;
		mask |= PLF2_TICKET_PLACE;
		if ( before && (before->m_street->m_flags2 & mask)) continue;
		if ( after  && (after ->m_street->m_flags2 & mask)) continue;

		// if we only got one that is good
		if ( ! before ) continue;
		if ( ! after  ) continue;
		// if same address essentially, allow it
		if ( before->m_hash == after->m_hash ) continue;
		// or if address have the same tag hash, keep our topologically
		// closest address as-is
		if ( before->m_section->m_tagHash ==
		     after ->m_section->m_tagHash    )
			continue;
		// if one address is in the todevent section and not the
		// other, then we are ok too
		Section *tods = ev->m_section;
		// scan up until we hit that
		for ( ; tods ; tods = tods->m_parent ) {
			// brethe
			QUICKPOLL(m_niceness);
			if ( tods->m_flags & SEC_TOD_EVENT ) break;
		}
		if ( tods && 
		     tods->contains(before->m_section) !=
		     tods->contains(after ->m_section) )
			continue;
		// ok, ambiguous
		ev->m_flags |= EV_AMBIGUOUS_LOCATION;
	}
	*/
	
	// set the indexed event ids to save space. we remove bad event
	// and re-use their event ids
	/*
	long newid = 1;
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// assume none
		ev->m_indexedEventId = 0;
		// get id
		long eid = ev->m_eventId;
		// skip if not valid
		if ( eid <= 0 ) continue;
		// ignore if over 255
		if ( eid > 255 ) continue;
		// skip if bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// set its hashed event id
		ev->m_indexedEventId = newid++;
		// stop if overflow
		if ( newid >= 256 ) break;
	}
	// save this. if none, will be 0
	m_maxIndexedEventId = newid - 1;
	// that is the # of valid events as well
	m_numValidEvents = newid - 1;
	*/

	///////////////////
	//
	// set Event::m_eventHash64
	//
	//////////////////
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// get id
		long eid = ev->m_eventId;
		// skip if not valid
		if ( eid <= 0 ) continue;
		// ignore if over 255
		if ( eid > 255 ) continue;
		// skip if bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// hash the words in the title
		long a = ev->m_titleStart;
		long b = ev->m_titleEnd;
		uint64_t titleHash64 = 0;
		for ( long i = a ; i < b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not alnum word
			if ( ! m_wids[i] ) continue;
			// xor it up otherwise
			titleHash64 <<= 1LL;
			titleHash64 ^= (uint64_t)m_wids[i];
		}
		// first set title hash, etc.
		ev->m_titleHash64   = titleHash64;
		// init to address hash
		uint64_t eventHash64 = ev->m_addressHash64;
		// integrate date hash
		eventHash64 = hash64h ( ev->m_dateHash64 , eventHash64 );
		// integrate title hash
		eventHash64 = hash64h ( ev->m_titleHash64 , eventHash64 );
		// store it
		ev->m_eventHash64 = eventHash64;

		///////////////////
		//
		// set Event::m_descHash
		//
		//////////////////

		// init hash
		uint64_t h = 0LL;
		// get first sentence event has that is valid
		Section *sent = m_sections->m_firstSent;
		// limit to first 50 sentences
		long count = 0;
		// loop over all sentences
		for ( ; sent ; sent = sent->m_nextSent ) {
			// breathe
			QUICKPOLL ( m_niceness );
			// skip if not in our description
			if ( ! sent->hasEventId ( eid ) ) continue;
			// hash each sentence up
			h ^= sent->m_sentenceContentHash;
			// up some
			h <<= 1LL;
			// stop after 50 sentences
			if ( ++count >= 50 ) break;
		}
		// set that
		ev->m_descHash64 = h;
		// make the dedup hash now too
		ev->m_dedupHash64 = hash64h ( ev->m_eventHash64 , h );
		
		///////////////////
		//
		// set Event::m_titleTagHash
		//
		//////////////////

		// title tag hash
		if ( ev->m_titleStart < 0 ) { char *xx=NULL;*xx=0; }
		ev->m_titleTagHash32 = 
			sp[ev->m_titleStart]->m_sentenceSection->m_tagHash;
	}

#ifdef _USETURKS_
	/*
	/////////////
	//
	// set Event::m_confirmedTitle and m_confirmedTitleContentHash32
	//
	// we have to do this here now instead of in XmlDoc::addTurkBits()
	// because TB_TURK_CANDIDATE depends on agreeing with the selected
	// title!
	//
	/////////////
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// get id
		long eid = ev->m_eventId;
		// skip if not valid
		if ( eid <= 0 ) continue;
		// ignore if over 255
		if ( eid > 255 ) continue;
		// skip if bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// shortcut, the title sentence section
		Section *ts = ev->m_titleSection->m_sentenceSection;
		// get its title sentence
		unsigned long long key = ((uint64_t)ts)<<32|(uint32_t)ev;
		//  lookup in turk votes
		turkbits_t *tbp = (turkbits_t *)m_tbt->getValue (&key);
		// is TB_TURK or TB_TURK_CANDIDATE bit set?
		if ( ! tbp ) continue;
		// skip if not
		if ( ! ( *tbp & (TB_TITLE|TB_TITLE_CANDIDATE) ) ) continue;
		// ok, confirmed title
		ev->m_confirmedTitle = true;
		ev->m_confirmedTitleContentHash32 = ts->m_sentenceContentHash;
	}
	
	/////////////
	//
	// set Event::m_confirmedVenue and m_confirmedVenueContentHash32
	//
	/////////////
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// get id
		long eid = ev->m_eventId;
		// skip if not valid
		if ( eid <= 0 ) continue;
		// ignore if over 255
		if ( eid > 255 ) continue;
		// skip if bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// skip if none
		//if ( ! ev->m_bestVenueName ) continue;
		// no, this is NULL if its "none in list"
		Place *venue = ev->m_bestVenueName;
		// lookup in turk table
		unsigned long long key = ((uint64_t)venue)<<32|(uint32_t)ev;
		//  lookup in turk votes
		turkbits_t *tbp = (turkbits_t *)m_tbt->getValue (&key);
		// is TB_TURK or TB_TURK_CANDIDATE bit set?
		if ( ! tbp ) continue;
		// skip if not
		if ( ! ( *tbp & (TB_VENUE|TB_VENUE_CANDIDATE) ) ) continue;
		// ok, confirmed title
		ev->m_confirmedVenue = true;
		// was it a vote for none?
		if ( ! venue ) {
			ev->m_confirmedVenueContentHash32 = 0;
			continue;
		}
		// otherwise, get the content hash of the place name
		ev->m_confirmedVenueContentHash32 =
			(uint32_t)venue->m_wordHash64;
	}
	// is "none" confirmed?
	*/
#endif




	//
	// set EV_PRIVATE (obituary or high school class schedule etc)
	//
	static bool s_init66 = false;
	static long long h_obituary;
	static long long h_obituaries;
	if ( ! s_init66 ) {
		s_init66 = true;
		h_obituary = hash64n("obituary");
		h_obituaries = hash64n("obituaries");
	}
	// get title of the entire document
	long sa = m_sections->m_titleStart;
	long sb = m_words->m_numWords;
	if ( sa < 0 ) { sa = 9999; sb = 0; }
	if ( sa + 50 < sb ) sb = sa + 50;
	bool isPrivate = false;
	for ( long i = sa ; i < sb ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// break on title
		if ( m_tids[i] == (TAG_TITLE|BACKBIT) ) 
			break;
		// look for obituary in the title or obituaries
		if ( ! m_wids[i] ) 
			continue;
		if ( m_wids[i] != h_obituary && 
		     m_wids[i] != h_obituaries )
			continue;
		// flag it
		isPrivate = true;
		// no need to go further
		break;
	}
	for ( long e = 0 ; isPrivate && e < m_numEvents ; e++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event
		Event *ev = &m_events[e];
		// skip if already bad
		if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// flag it
		ev->m_flags |= EV_PRIVATE;
	}



	return true;
}

// set ev->m_bestVenue ptr to a Place name
bool Events::setBestVenueName ( Event *ev ) {

#ifdef _USETURKS_
	/*
	// . check to see if "none" was a verified turk
	// . use the NULL section, so use "0" for section ptr
	uint64_t key = (((uint64_t)000000)<<32) | (uint32_t)ev;
	// get its turkbits
	turkbits_t *tbp = (turkbits_t *)m_tbt->getValue (&key);
	// if set...
	if ( tbp && (*tbp & TB_VENUE) ) {
		// the turks agreed they could not find one on this page
		ev->m_bestVenueName = NULL;
		// that's that
		return true;
	}
	*/
#endif

	//Place *winner = NULL;
	//long   maxScore = 0;

	// get name1 from current address if it exists
	// THIS is often aliases to the actual address of ANOTHER event
	// like in the case of unm.edu St. Vincent Society clothing exchange
	// event...
	Place *winner = ev->m_address->m_name1;
	long   maxScore = 0;
	// is it there?
	if ( winner ) {
		maxScore += 100;
		// if there, is it verified
		if ( ev->m_address->m_flags & AF_VERIFIED_PLACE_NAME_1 ) 
			maxScore +=100;
#ifdef _USETURKS_
		/*
		// is name in document?
		long a = winner->m_a;
		if ( a >= 0 ) {
			// get its section
			//Section *sx = m_sections->m_sectionPtrs[a];
			// sentence
			//Section *si = sx->m_sentenceSection;
			// get turk bits for that
			uint64_t key = (((uint64_t)winner)<<32) | (uint32_t)ev;
			// get its turkbits
			turkbits_t *tbp = (turkbits_t *)m_tbt->getValue (&key);
			// shortcut
			if ( tbp && (*tbp & TB_VENUE    ) ) maxScore += 1000;
			if ( tbp && (*tbp & TB_NOT_VENUE) ) maxScore -= 1000;
		}
		*/
#endif
	}


	// a reject?
	if ( maxScore < 0 ) winner = NULL;

#ifdef _USETURKS_
	/*
	PlaceMem *sm = &m_addresses->m_sm;
	PlaceMem *pm = &m_addresses->m_pm;
	PlaceMem *PM = sm;
	long bigCount = 0;

 bigloop:

	// loop over them
	for ( long i = 0 ; i < PM->getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Place *pp = (Place *)PM->getPtr(i);
		// is it a name?
		bool isName  = pp->m_flags2 & PLF2_IS_NAME ;
		// if we are a building/venue name isName will be true
		if ( PM == sm && ! isName ) continue;
		// for the other guy
		if ( PM == pm &&
		     pp->m_type != PT_STREET &&
		     pp->m_type != PT_NAME_1 &&
		     pp->m_type != PT_NAME_2 ) 
			continue;
		// shortcuts
		//long a = p->m_a;
		//long b = p->m_b;
		// skip if not in document
		if ( pp->m_a < 0 ) continue;
		// get seciton ptr
		Section *sp = m_sections->m_sectionPtrs[pp->m_a];
		// get its section sentence
		Section *si = sp->m_sentenceSection;
		// we now have the new requirement that is must be the
		// full sentence! otherwise we are not sure which subsentence
		// is the true place name!
		// so "The Love Song of J. Robert Oppenheimer by Caron"
		// which has like 3 different place names can be confusing,
		// and this fixes stuff like that. or
		// "Filling Station in Albuquerque" has two...
		// "Filling Station" and "Filling Station in Albuquerque"
		//if ( si->m_a != a ) continue;
		//if ( si->m_b != b ) continue;
		// skip if not for us
		if ( ev->m_eventId < si->m_minEventId ) continue;
		if ( ev->m_eventId > si->m_maxEventId ) continue;
		// get our event id as a byte offset and bit mask
		unsigned char byteOff = ev->m_eventId / 8;
		unsigned char bitMask = 1 << (ev->m_eventId % 8);
		// make sure our bit is set
		if ( ! ( si->m_evIdBits[byteOff] & bitMask ) ) continue;
		// ge this score
		long score = 0;
		// get turk bits for that
		uint64_t key = (((uint64_t)pp)<<32) | (uint32_t)ev;
		// get its turkbits
		turkbits_t *tbp = (turkbits_t *)m_tbt->getValue (&key);
		// shortcut
		if ( tbp && (*tbp & TB_VENUE) ) score += 1000;
		// new winner?
		if ( score <= maxScore ) continue;
		// yes
		maxScore = score;
		winner = pp;
	}

	// try next set of place names
	if ( ++bigCount == 1 ) {
		PM = pm;
		goto bigloop;
	}
	*/
#endif

	// if it was unverfied we end up getting crap like
	// "How To Contact US" etc.
	if ( maxScore == 100 && winner ) {
		// assume contains no place indicator
		bool gotIt = false;
		// kill the winner if does not contain a place indicator
		for ( long i = winner->m_a ; i < winner->m_b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// check it out
			if ( isPlaceIndicator ( &m_wids[i] ) ) gotIt = true;
		}
		// require this if we have nothing else...
		if ( ! gotIt ) winner = NULL;
	}

	// assign the winner, might be NULL
	ev->m_bestVenueName = winner;

	return true;
}

class TitleCand {
public:
	long m_a;
	long m_b;
	float m_titleScore;
};

// for comparing the title sentence to another sentence to see if they
// are kinda like duplicates
float Events::getSimilarity ( long a0 , long b0 , long a1 , long b1 ) {

	long long *wids = m_words->getWordIds  ();
	
	long vec0[64];
	long vec1[64];
	long nv0 = 0;
	long nv1 = 0;
	// make vector 0
	for ( long i = a0 ; i < b0 ; i++ ) {
		// skip if not alnumword
		if ( ! wids[i] ) continue;
		// add it otherwise
		vec0[nv0++] = (long)wids[i];
		// stop if would breach
		if ( nv0 >= 63 ) break;
	}
	// make vector 1
	for ( long i = a1 ; i < b1 ; i++ ) {
		// skip if not alnumword
		if ( ! wids[i] ) continue;
		// add it otherwise
		vec1[nv1++] = (long)wids[i];
		// stop if would breach
		if ( nv1 >= 63 ) break;
	}
	// null term
	vec0[nv0] = 0;
	vec1[nv1] = 0;
	// use this to compute the similarity, its in XmlDoc.cpp
	return computeSimilarity ( vec0 ,
				   vec1 ,
				   NULL ,
				   NULL ,
				   NULL ,
				   m_niceness ,
				   false      );
}

bool Events::setTitle ( Event *ev ) {

	// reset subsentences array
	//s_nss = 0;

	// reset the title scores for event
	ev->m_titleScore = -1.0;
	ev->m_titleSection = NULL;

	// shortcut
	Section **sp = m_sections->m_sectionPtrs;

	// now we do a heterogenous loop
	long i2 = 0;
	long tsa;
	long tsb;
	esflags_t esflags;
	sentflags_t sentFlags;
	SubSent *sub = NULL;
	Section *si;

	TitleCand tcands[1000];
	long ntd = 0;
	long maxDupVotes = 0;

	// get date
	Date *dd = ev->m_date;
	// unm.edu has a partial "store hours" on sunday...
	// "Summer Hours: March 15-Oct. 15:8 am. Mon - Fri, 
	// 7:30 am-10 am Sun." so we need to mark such dates as 
	// DF_STORE_HOURS
	bool isStoreHoursEvent = ( dd->m_flags & DF_STORE_HOURS );
	bool winnerIsSubSent = false;

	// now pick the best title for each event
	for ( Section *sk = m_sections->m_firstSent ; ; ) {
		// breathe
		QUICKPOLL(m_niceness);
		// advance if still doing full sentences
		if ( sk ) {
			// assign
			si = sk;
			// advance for next round
			sk = sk->m_nextSent;
			// set word boundaries
			tsa = si->m_senta;
			tsb = si->m_sentb;
			// get event/sent flags for full sentence
			esflags = getEventSentFlags(ev,si,m_evsft);
			// assign this
			sentFlags = si->m_sentFlags;
			// set max dup vote count
			if ( si->m_votesForDup > maxDupVotes )
				maxDupVotes = si->m_votesForDup;
		}
		// if full sentences exhausted, use subsentences
		else if ( ! sk ) {
			// check the subs, if those are exhausted, stop!
			if ( i2 >= m_numSubSents ) break;
			// otherwies, grab one
			sub = &m_subSents[i2++];
			// assign si
			si = sp[sub->m_senta]->m_sentenceSection;
			// assign word boundaries
			tsa = sub->m_senta;
			tsb = sub->m_sentb;
			// get event/sent flags for SUBsentence
			esflags = getEventSubSentFlags(ev,sub,m_evsft);
			// special
			//sentFlags = getSubSentFlags (si, tsa, tsb);
			sentFlags = sub->m_subSentFlags;
			// . if it is in menu forget it
			// . fixes residentadvisor.net from using a menu
			//   title
			if ( sentFlags & SENT_IN_MENU ) continue;
		}
		// sanity check - need sentence
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) { char *xx=NULL;*xx=0;}
		// skip if not in description right now, no need to score it!
		if ( si->m_minEventId <= 0 ) continue;
		// must be us
		if ( ! si->hasEventId ( ev->m_eventId ) ) continue;

		// . skip it then (SEC_SPLIT_SENT)
		// . we need this lest we print out "had no title" because
		//   the event display in makeEventDisplay2() is empty!!
		if ( si->m_senta < si->m_a ) continue;
		// do not include tag indicators in summary ever
		if ( si->m_sentFlags & SENT_TAG_INDICATOR ) continue;

		// desc score
		float dscore = 0.0;
		// set score of the sentence
		float tscore = getSentTitleScore ( si     ,
						   sentFlags,
						   esflags,
						   tsa    ,
						   tsb    ,
						   isStoreHoursEvent ,
						   ev ,
						   &dscore ,
						   sub );
		// . we save in these tables for debug output/display
		// . TODO: not for subsent though?
		if ( ! sub ) {
			// mix it up!
			uint32_t evkey = hash32h((uint32_t)ev,12345);
			// make the key
			uint64_t key = (((uint64_t)si)<<32) | (uint32_t)evkey;
			if ( ! m_titleScoreTable.addKey(&key,&tscore) ) 
				return false;
			if ( ! m_descScoreTable .addKey(&key,&dscore) ) 
				return false;
		}

		// store it here for debug output i guess
		if ( sub ) sub->m_titleScore = tscore;

		// need it positive
		if ( tscore <= 0 ) continue;

		// store in our list of title candidates for setting
		// the EV_HASTITLEBYVOTES bit algo below
		if ( ntd < 1000 ) {
			tcands[ntd].m_a          = tsa;
			tcands[ntd].m_b          = tsb;
			tcands[ntd].m_titleScore = tscore;
			ntd++;
		}

		// it is better than what we got now?
		if ( ev->m_titleScore > tscore ) continue;
		// if no tie, assign
		if ( ev->m_titleScore != tscore ) {
		gotwinner:
			// ok, we got a new winner
			ev->m_titleScore   = tscore;
			ev->m_titleStart   = tsa;//si->m_senta; // a;
			ev->m_titleEnd     = tsb;//si->m_sentb; // b;
			ev->m_titleSection = si;
			if ( sub ) winnerIsSubSent = true;
			else       winnerIsSubSent = false;
			continue;
		}
		//
		// if tied, prefer the one closet to event date/tod
		//
		// is this possible?
		if ( ev->m_date->m_a < 0 ) {
			if(si->m_senta<ev->m_titleStart)goto gotwinner;
			continue;
		}
		// . now we must base on sections, not words!
		// . so first compute topological distance by seeing
		//   who has the first parent in common with the 
		//   base date's section
		// . if same, then prefer the one on top?
		// . if both above, date, prefer closest!
		Section *ds = sp[ev->m_date->m_a ]->m_sentenceSection;
		Section *s1 = sp[ev->m_titleStart]->m_sentenceSection;
		Section *s2 = sp[si->m_senta     ]->m_sentenceSection;
		
		// winner?
		bool s1wins = false;
		bool s2wins = false;
		// now if the date were to telescope out, which 
		// section would it contain first? s1 or s2?
		for ( ; ; ds = ds->m_parent ) {
			// breathe
			QUICKPOLL(m_niceness);
			// stop if contains date
			if ( ds->contains ( s1 ) ) s1wins = true;
			if ( ds->contains ( s2 ) ) s2wins = true;
			// stop if got one
			if ( s1wins || s2wins ) break;
		}
		
		// incumbent clear winner? if so, skip si
		if ( s1wins && ! s2wins ) continue;
		// incombent loeses?
		if ( s2wins && ! s1wins ) goto gotwinner;
		// ok, we got a tie
		Section *next;
		// . count # of sentence sections in between the two
		// . prefer guy closest to date section then
		ds = sp[ev->m_date->m_a ]->m_sentenceSection;
		
		// set this
		bool beforeTitle = (s1->m_a < ds->m_a);
		
		// scan sentences in between s1 and d1 and count
		// only those sentences that do not have
		// SENT_HASSOMEEVENTSDATE set
		long s1count = 0;
		for ( ; s1 != ds ; s1 = next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . if this changes, stop on that as well.
			// . fixes swoggled tables
			bool nowBefore = ( s1->m_a < ds->m_a );
			if ( nowBefore != beforeTitle ) break;
			// set next sent
			if ( beforeTitle ) next = s1->m_nextSent;
			else               next = s1->m_prevSent;
			// skip if has flag set
			if ( !(s1->m_sentFlags&SENT_HASSOMEEVENTSDATE))
				s1count++;
		}
		
		// set this
		beforeTitle = (s2->m_a < ds->m_a);
		
		// scan sentences in between s2 and d1 and count
		// only those sentences that do not have
		// SENT_HASSOMEEVENTSDATE set
		long s2count = 0;
		for ( ; s2 != ds ; s2 = next ) {
			// breathe
			QUICKPOLL(m_niceness);
			// . if this changes, stop on that as well.
			// . fixes swoggled tables
			bool nowBefore = ( s2->m_a < ds->m_a );
			if ( nowBefore != beforeTitle ) break;
			// set next sent
			if ( beforeTitle ) next = s2->m_nextSent;
			else               next = s2->m_prevSent;
			// skip if has flag set
			if ( !(s2->m_sentFlags&SENT_HASSOMEEVENTSDATE))
				s2count++;
		}
		// prefer one above first... no
		// misses "bird walk" etc.
		//if ( dist1 < 0 && dist2 >= 0 ) continue;
		// prefer greater distance if on top
		// try to fix bad titles of non-headliners for
		// reverbnation.com.
		// make positive
		//if ( dist1 < 0 ) dist1 *=-1;
		//if ( dist2 < 0 ) dist2 *=-1;
		// compare
		if ( s1count <= s2count ) continue;
		// we are the winner, add us in
		goto gotwinner;
	}

	// if event is a subsentence, consider indicated as well!
	if ( winnerIsSubSent )
		ev->m_flags |= EV_HASTITLESUBSENT;//INDICATOR;

	// i've seen a core because this was NULL, could not reproduce it
	// but was for http://georgiachess.org/calendarix/cal_day.php?op=
        // day&date=2011-12-09&catview=0
	if ( ! ev->m_titleSection )
		return true;

	// index events with strong titles we index gbeventindicator:1
	if ( ev->m_titleSection->m_sentFlags & SENT_HASTITLEWORDS )
		ev->m_flags |= EV_HASTITLEWORDS;//INDICATOR;
	// or if pre-word was like "title:"
	if ( ev->m_titleSection->m_sentFlags & SENT_INTITLEFIELD)
		ev->m_flags |= EV_HASTITLEFIELD;//INDICATOR;

	// do not do the algo below if one of these is set to avoid confusion!
	//if ( ev->m_flags & EV_HASTITLESUBSENT ) return true;
	//if ( ev->m_flags & EV_HASTITLEWORDS   ) return true;
	//if ( ev->m_flags & EV_HASTITLEFIELD   ) return true;

	// . now compare the winner to the other titles.
	// . if similarity is >= 50%, skip the comparison (skip dups!)
	// . find how much greater his title score is than the next highest
	// . then the title section needs the following in order for us to set
	//   the EV_HASTITLEBYVOTES bit:
	//    * his title score is 3x the next highest from those compared
	//    * his title score >= 5.0
	//    * his notdupvotes >= 1
	//    * his notdupvotes >= his dupvotes
	//    * maxdupvotes >= 2 (max is for all sections on this page)
	//    * his 2*dupvotes <= maxdupvotes
	// . the logic: there are 3+ events on this site with 
	//   different titles. but if one is a lot more heavily represented
	//   then we could lose it because its dupvotes might be quite close
	//   to the maxdupvotes... however, we'd at least get the other 2
	//   events!
	// . NOTE: we might have to use getContentHash32Fast() for section
	//   voting in order to treat date words and numbers as one entity
	//   otherwise they could be seen a non-dup sections and could get 
	//   their EV_HASTITLEBYVOTES bit set!
	// . that means we might have to rebuild sectiondb!!

	long long *wids = m_words->getWordIds  ();
	char **wptrs = m_words->getWords();
	long  *wlens = m_words->getWordLens();

	// . did the winning title have a date in it? if so nuke it because
	//   the votes will say "not dup" but really it is "dup" but just
	//   with a different date. like "Calendar January 2012" or
	//   "12-23-2011, 03:14 AM Hello There, Guest!"
	// . i have updated Sections.cpp to use Section::m_voteHash32 for
	//   the section content hash which now ignores dates and numbers,
	//   however, it will take a while for sectiondb to be stocked up
	//   with these new vote content hashes. 
	// . when sectiondb is finally stocked up we can remove this logic
	//   safely i would guess.. there are some titles that might have
	//   a date in them but the rest of the words are unique!
	// . NO! i took the votehash32 out of sections.cpp for simplicity.
	//   we should just use our new algo which sets hastitlewords if
	//   a title has an am/pm tod time in it, provided it is not just
	//   a place name and other generics there...
	bool hasMonth = false;
	bool hasDayNum = false;
	bool hasNumber = false;
	for ( long i = ev->m_titleStart ; i < ev->m_titleEnd ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not word
		if ( ! wids[i] ) continue;
		// really just limit to month names for now
		//if ( D_IS_MONTH ) hadMonth = true;
		//if ( D_IS_DAYNUM ) hadDaynum = true;
		if ( ! m_bits ) continue;
		// if its a number and not a date, do not allow it. it
		// could be the outside temperature or ticket price, but
		// it will trigger hight notdupvotes when in reality all
		// numbers should be hashed as one number and it should have
		// a high dup count. fixes the "44.0F" title from getting
		// its hastitlebyvotes flag set.
		if ( !(m_bits[i] & D_IS_IN_DATE) ) {
			// a non-date number?
			if ( ! is_digit(wptrs[i][0]) ) continue;
			// bad bad
			hasNumber = true;
			break;
		}
		// skip if not date word
		if ( !(m_bits[i] & D_IS_IN_DATE) ) continue;
		// tod date is ok ("Shabbat at 6pm")
		if ( m_bits[i] & D_IS_MONTH ) hasMonth = true;
		// i guess a year is ok too!
		if ( m_bits[i] & D_IS_DAYNUM ) hasDayNum = true;
	}
	//  nuke it if like "April 2010" calendar header
	//if ( hasMonth && ! hasDayNum ) ntd = 0;
	// no, no, now daynum is bad too! sometimes we get the date
	// as the title because of the daynum
	if ( hasMonth || hasDayNum ) ntd = 0;
	// a non-date number is bad (see above for reason)
	if ( hasNumber ) ntd = 0;
	// do not bother if we breached
	if ( ntd >= 1000 ) ntd = 0;
	// . or if our score is too low
	// . if its got EVSENT_GENERIC_WORDS or SENT_BAD_FIRST_WORD or
	//   SENT_BAD_EVENT_START set then hopefully the title score will be 
	//   too low to pass this...
	if ( ev->m_titleScore < 5.0 ) ntd = 0;
	// get winning title section
	Section *ws = ev->m_titleSection;
	// winning title dup votes check. must have a "notdup" vote
	if ( ws->m_votesForNotDup <= 0 ) ntd = 0;
	// votes for not dup must outweight (or equal) votes for dup
	if ( ws->m_votesForNotDup < ws->m_votesForDup ) ntd = 0;
	// must be twice the max dup
	if ( ws->m_votesForDup * 2 > maxDupVotes ) ntd = 0;
	// phone number is bad!
	if ( ws->m_sentFlags & SENT_HAS_PHONE ) ntd = 0;
	// get this -- only store hours i guess!!
	if ( ! ( ev->m_date->m_flags & DF_STORE_HOURS ) ) {
		if ( ws->m_sentFlags & SENT_PLACE_NAME          ) ntd = 0;
		if ( ws->m_sentFlags & SENT_CONTAINS_PLACE_NAME ) ntd = 0;
		if ( ws->m_sentFlags & SENT_OBVIOUS_PLACE       ) ntd = 0;
	}
	// "No Evening Service"
	if ( ws->m_sentFlags & SENT_BADEVENTSTART  ) ntd = 0;
	if ( ws->m_sentFlags & SENT_BAD_FIRST_WORD ) ntd = 0;
	// toss out if mixed case (2+ lower case words) just because
	// we are getting huge sentences... but this is hurting good ones too!
	//if ( ws->m_sentFlags & SENT_MIXED_CASE ) ntd = 0;
	// assume the winning title IS distinct enough
	bool distinctEnough = true;
	// but not if we zero'ed out ntd
	if ( ntd == 0 ) distinctEnough = false;
	// loop over all title candidates this event had
	for ( long i = 0 ; i < ntd ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if it is the winning title
		if ( tcands[i].m_a == ev->m_titleStart &&
		     tcands[i].m_b == ev->m_titleEnd ) 
			continue;
		// if intersects with winning title, skip it
		if ( tcands[i].m_a >= ev->m_titleStart &&
		     tcands[i].m_a <  ev->m_titleEnd )
			continue;
		if ( tcands[i].m_b >  ev->m_titleStart &&
		     tcands[i].m_b <= ev->m_titleEnd )
			continue;
		// if its score is less than 1/3 of winning title score then
		// forget it, it's not going to hurt us.
		if ( tcands[i].m_titleScore * 3.0 <= ev->m_titleScore )
			continue;
		// otherwise, how similar is it to the winning title?
		float sim = getSimilarity ( ev->m_titleStart ,
					    ev->m_titleEnd ,
					    tcands[i].m_a ,
					    tcands[i].m_b );
		// if too similar skip it, might be a dup
		if ( sim >= 50.0 ) continue;
		// ok, the winning title is not distinct enough, so give up
		distinctEnough = false;
		break;
	}	
	// set this if its distinct enough. we will include it in resultset #1
	if ( distinctEnough )
		ev->m_flags |= EV_HASTITLEBYVOTES;

	if ( ev->m_date->m_flags & DF_TIGHT )
		ev->m_flags |= EV_HASTIGHTDATE;

	if ( ev->m_date->m_flags & DF_INCRAZYTABLE )
		ev->m_flags |= EV_INCRAZYTABLE;


	// set EV_HASTITLEWITHCOLON (similar to hastitlewords) but i do not
	// want to reward such titles with a high score in the title setting
	// algo cuz they get crap like "Room: Mill's Lawn" or some other
	// fielded term.
	long numNonGenericAlphas = 0;
	bool hadColon = false;
	nodeid_t *tids = m_words->getTagIds();
	long a = ws->m_senta;
	long b = ws->m_sentb;
	if ( ! (ws->m_sentFlags & SENT_HAS_COLON) ) b = a;
	static long long h_am;
	static long long h_pm;
	static long long h_http;
	static bool s_init33 = false;
	if ( ! s_init33 ) {
		s_init33 = true;
		h_am = hash64n("am");
		h_pm = hash64n("pm");
		h_http = hash64n("http");
	}
	for ( long i = a ; i < b ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		if ( wids[i] ) {
			// fix "hits: 2" title
			if ( !(m_bits[i]&D_CRUFTY) && is_alpha_a(wptrs[i][0]) )
				numNonGenericAlphas++;
			continue;
		}
		if ( tids[i] ) continue;
		if ( i == a ) continue;
		if ( i + 1 >= b ) continue;
		if ( ! m_words->hasChar(i,':') ) continue;
		// do not allow "10:30" to trigger!
		if ( is_digit(wptrs[i-1][wlens[i-1]-1]) &&
		     is_digit(wptrs[i+1][0]           ) )
			continue;
		// require an alnum word right before
		if ( ! wids[i-1] ) continue;
		if ( ! wids[i+1] ) continue;
		// and word must not be generic like
		// "location:" or "date:"
		bool crufty = false;
		if ( m_bits[i-1] & D_CRUFTY ) crufty = true;
		// allow 7:30 pm: blah blah though!
		if ( wids[i-1] == h_am ) crufty = false;
		if ( wids[i-1] == h_pm ) crufty = false;
		if ( crufty ) continue;
		// fix City+of+Seaside,+CA+:+City+Clerk
		if (           getStateDesc ( wptrs[i-1] ) ) continue;
		if ( i-3>=a && getStateDesc ( wptrs[i-3] ) ) continue;
		// fix http://
		if ( wids[i-1] == h_http ) continue;
		// . if all words to right are generic that's bad
		// . fix Mohawk+Mountain+Ski+Area+:+Home
		bool rightCrufty = true;
		for ( long k = i + 1 ; k < b ; k++ ) {
			if ( ! wids[k] ) continue;
			if ( m_bits[k] & D_CRUFTY ) continue;
			rightCrufty = false;
			break;
		}
		if ( rightCrufty ) continue;
			
		// a place indicator on the left is bad. fixes
		// Where:+Pumping+Station:+One
		// but hurts:
		// Mohawk+Mountain+Ski+Area+:+Fab+Fridays
		if ( isPlaceIndicator ( &wids[i-1] ) ) continue;
		
		
		

		hadColon = true;
		break;
	}
	if ( hadColon && numNonGenericAlphas >= 2 ) 
		ev->m_flags |= EV_HASTITLEWITHCOLON;


	//if ( ev->m_date->m_flags & DF_FACEBOOK )
	if ( strncmp(m_url->m_url,"http://www.facebook.com/event",29)==0 )
		ev->m_flags |= EV_FACEBOOK;

	if ( strncmp(m_url->m_url,"http://www.stubhub.com/",23)==0 )
		ev->m_flags |= EV_STUBHUB;

	if ( strncmp(m_url->m_url,"http://www.eventbrite.com/",26)==0 )
		ev->m_flags |= EV_EVENTBRITE;

	// scan for hide_guest_list for facebook events
	long     n     = m_xml->getNumNodes();
	XmlNode *nodes = m_xml->getNodes();
	if ( ! ( ev->m_flags & EV_FACEBOOK ) ) n = 0;
	for ( long i = 0 ; i < n ; i++ ) {
		if ( nodes[i].m_nodeId != TAG_FBHIDEGUESTLIST ) continue;
		// must be front tag
		if ( ! nodes[i].isFrontTag() ) continue;
		// get value
		char *s = nodes[i].m_node + nodes[i].m_nodeLen;
		long val = atol(s);
		if ( val ) ev->m_flags |= EV_HIDEGUESTLIST;
	}


	/*
	// now set TSF_BEST_TITLE for tieing title scores 
	// adjacent to title
	Section *sa = ev->m_titleSection;
	// scan up and set those
	for ( ; sa ; sa = sa->m_prev ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no text
		if ( sa->m_flags & SEC_NOTEXT ) continue;
		// stop if not ours
		if ( ! sa->hasEventId (ev->m_eventId) ) break;
		// check score
		if ( sa->m_titleScore < ev->m_titleScore ) break;
		// skip if already set
		if ( sa->m_sentFlags & TSF_BEST_TITLE ) continue;
		// set it
		sa->m_sentFlags |= TSF_BEST_TITLE;
		// when selecting event titles out of the EventDesc
		// candidates, make it easy!
		sa->m_titleScore += 1000.0;
		// and hash it with some weight so it comes up on top
		// so the soul power comes up for 'soul' search
		sa->m_descScore += 800.0;
	}
	// and going forward too
	sa = ev->m_titleSection;
	// scan up and set those
	for ( ; sa ; sa = sa->m_next ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if no text
		if ( sa->m_flags & SEC_NOTEXT ) continue;
		// stop if not ours
		if ( ! sa->hasEventId (ev->m_eventId) ) break;
		// check score
		if ( sa->m_titleScore < ev->m_titleScore ) break;
		// skip if already set
		if ( sa->m_sentFlags & TSF_BEST_TITLE ) continue;
		// set it
		sa->m_sentFlags |= TSF_BEST_TITLE;
		// when selecting event titles out of the EventDesc
		// candidates, make it easy!
		sa->m_titleScore += 1000.0;
		// and hash it with some weight so it comes up on top
		// so the soul power comes up for 'soul' search
		sa->m_descScore += 800.0;
	}
	*/

	// all done
	return true;
}

// set score of the sentence
float Events::getSentTitleScore ( Section *si ,
				  sentflags_t sflags ,
				  esflags_t esflags ,
				  long sa ,
				  long sb ,
				  bool isStoreHoursEvent ,
				  Event *ev ,
				  float *retDescScore ,
				  bool isSubSent ) {

	// get the sentence flags for this
	//sentflags_t sflags = si->m_sentFlags;
	
	// init title and event desc score
	float tscore = 100.0;
	float dscore = 100.0;

	// . give it a huge boost for being in this format
	// . undo periodends penalties, etc.
	// . "$4 Donation, 8 a.m. - 1 p.m." was exception
	//   to this but we added "donation" to the ignore
	//   list in Sections.cpp since it is like "ticket"
	if ( isSubSent ) {
		// to be a subsentence you must have been indicated by
		// an indicator like "joe smith presents..."
		tscore *= 1.10;
		// mixed? even if just one word is lower case, count it
		// as mixed case for this algo. we have to be extremely
		// strict for subsentence titles otherwise crap like
		// '"This series" runs from' from denver.org gets through.
		bool mixed = (sflags & SENT_MIXED_CASE_STRICT);
		// any event ending counteracts that to fix abqtango.com
		// "Free introductory Argentine Tango dance class, Tuesdays,"
		//if ( sflags & SENT_EVENT_ENDING    ) mixed = false;
		if ( sflags & SENT_HASTITLEWORDS   ) mixed = false;
		// fix "he will be presenting trends..." for seattle24x7.com
		if ( mixed ) tscore *= .30;
		// and too long
		if ( sb - sa > 30 ) tscore *= .30;
		// mixed case and long? (fixes frenchtulip.com)
		if ( mixed && sb - sa > 30 ) tscore *= 0.25;
	}

	// . a format dup?
	// . a lot of times we have multiple events on a page but only
	//   one is recognized (msihicago.org) and therefore format dup
	//   hurts us because the title will be in a format dup... and
	//   multevents will never be set... so only apply this penalty
	//   if we have >1 valid events...
	// . seems to hurt hardwoodmueseum.com too, try taking out. seems
	//   better to take it out, seems like it is often kinda of a 
	//   hit or miss thing since it is based on tags!!
	// . but DO keep the bit set for use in other functions
	// . nah, leave it in because more than 50% of the time i think it
	//   helps rather than hurts. and the hardwoodmuseum page doesn't
	//   have a clear title anyway...
	if ( esflags & EVSENT_FORMAT_DUP ) {
		// do not punish if page only has one event detected
		//if ( m_numValidEvents > 1 )
		tscore *= .99;
	}

	if ( sflags & SENT_INNONTITLEFIELD )
		tscore *= .15;

	bool gotInTitleFieldBonus = false;
	if ( sflags & SENT_INTITLEFIELD ) {
		// this was *3.0 but boosted to 5.0 to beat SENT_HASTITLEWORDS
		// for stubhub.com...
		tscore *= 5.0;
		gotInTitleFieldBonus = true;
	}
	// disregard being in a form table if we are in a title field!
	else {
		if ( sflags & SENT_FORMTABLE_FIELD )
			tscore *= .05;
		if ( sflags & SENT_FORMTABLE_VALUE )
			tscore *= .05;
	}

	if ( sflags & SENT_STRANGE_PUNCT ) 
		tscore *= .85;
	if ( sflags & SENT_IS_DATE )
		tscore *= .99;
	if ( sflags & SENT_LAST_STOP )
		tscore *= .15;
	if ( sflags & SENT_CITY_STATE )
		tscore *= .10;
	if ( sflags & SENT_HAS_PHONE )
		tscore *= .50;
	if ( sflags & SENT_PERIOD_ENDS ) 
		tscore *= .999;
	if ( sflags & SENT_PERIOD_ENDS_HARD )
		tscore *= .75;
	if ( sflags & SENT_PARENS_START )
		tscore *= .05;
	if ( sflags & SENT_AFTER_SENTENCE )
		tscore *= .05;
	if ( sflags & SENT_FIELD_NAME )
		tscore *= .02;

	// fix 'bernalillo counties' title for graypanthers.com
	if ( sflags & SENT_BADEVENTSTART )
		tscore *= .10;

	// yes it is?
	bool good = true;
	// to fix "Slide Show" for abqfolkdance.org. its a menu thing.
	if ( (sflags & SENT_IN_MENU) && (esflags & EVSENT_FORMAT_DUP) )
		good = false;
	// fixes "Presented By Colorado Symphony Orchestra" for denver.org
	if ( sflags & SENT_BADEVENTSTART )
		good = false;
	// fix "Join our e-group" where group is an event ending
	// but "Join" is a bad first word
	if ( sflags & SENT_BAD_FIRST_WORD ) 
		good = false;
	// . do not apply this bonus if is also an obvious place name
	// . because "night club" is an obvious place name but "club"
	//   is an event ending. but it should be precluded.
	// . should fix guysndollsllc.com from picking the place name
	//   as the title rather than the cancer event title.
	if ( sflags & SENT_OBVIOUS_PLACE )
		good = false;

	// if title has "market" or "markets" or "fair" and store hours
	// is set, do not set good to false, because it such words
	// are indicative of events as well as places.
	// without this villr.com gets "Markets" as its title because it
	// gets the 1.08 isSubSent bonus above and the main title which is
	// "Los Ranchos Growers' and Arts/Crafts Markets" fails because
	// isStoreHoursEvent is true! however, we do not set EV_STOREHOURS
	// flag because the date, even though IT has DF_STOREHOURS set,
	// contains a daynum and no season, so the EV_STOREHOURS algo allows
	// it through!
	static long long h_market;
	static long long h_markets;
	static long long h_fair;
	static bool      s_init22 = false;
	if ( ! s_init22 ) {
		s_init22 = true;
		h_market  = hash64n("market");
		h_markets = hash64n("markets");
		h_fair    = hash64n("fair");
	}
	long long *wids = m_words->getWordIds  ();
	bool bothPlaceAndEvent = false;
	if ( wids[sb-1] == h_market  || 
	     wids[sb-1] == h_markets || 
	     wids[sb-1] == h_fair    )
		bothPlaceAndEvent = true;
		

	// if store hours though, do not use this bonus, it is meant
	// for events, not place names
	if ( isStoreHoursEvent && ! bothPlaceAndEvent )
		good = false;

	// . did we get the intitlefield bonus?
	// . do not allow a double bonus because a table has Title in it
	//   but is referring to a job title for: www.newamericaschoolnm.org
	//   and the title is "Admin/Reading" and Reading is a title word...
	// . no then we lose "A TUNA CHRISTMAS" from santafeplayhouse.org
	//   because Currently Playing at the Sante Fe Playhouse" has
	//   "at" (title words) and wins
	//if ( gotInTitleFieldBonus )
	//	good = false;

	// "IPhone Boot Camp"? etc. see all the good endings in Sections.cpp
	if ( (sflags & SENT_HASTITLEWORDS) && good )
		// a big boost!
		tscore *= 3.0;

	// if in a table that has a dow date header and we are in column
	// one with a title word or indicator, then totally boost us. this
	// is kinda like being in a title field. should fix up
	// westernballet.org some
	if ( (sflags & SENT_HASTITLEWORDS) && good &&
	     (ev->m_date->m_flags & DF_TABLEDATEHEADERROW) &&
	     si->m_colNum == 1 &&
	     si->m_rowNum >= 2 )
		tscore *= 2.0;

	// "FESTIVAL of lights"
	//else if (  (sflags & SENT_GOODEVENTSTART) && good )
	//	tscore *= 3.0;

	// now that we include colons in sentence as long as there
	// is not a tag separation, we can punish these things more
	// as if they are part of a descriptor table. like
	// "tickets: $10\nsauna: yes\nhottub: no\n"
	//if ( sflags & SENT_COLON_ENDS )
	//	tscore *= .05;
	// . after a colon after another sentence(s) on the same
	//   line! i.e. 
	//   "Hotel Rates:" 
	//   "Multiple hotels in close proximity."
	//   "Tournament rate to-be-negotiated.\n"
	//   - from tennisoncampus.com
	// . set Section::m_vertLine
	//if ( (sflags & SENT_AFTER_COLON) &&
	//     // but if its after a "title:" field, do not punish
	//     ! (sflags & SENT_TITLE_INDICATED) )
	//	tscore *= .10;
	
	
	// title indicator is x 10.0 so make this x.09
	// to undo that plus a little less
	if ( sflags & SENT_MIXED_CASE )
		tscore *= .09;
	if ( sflags & SENT_IS_BYLINE )
		tscore *= .05;
	// fix "ESL & Mike Thrasher Present"
	if ( sflags & (SENT_TITLE_FIELD | SENT_NON_TITLE_FIELD) ) {
		tscore *= .50;
		dscore *= .50;
	}
	if ( sflags & SENT_POWERED_BY ) {
		tscore *= .20;
		dscore *= .20;
	}
	
	// do not even index if a menu
	if ( si->m_flags & (SEC_MENU | SEC_MENU_HEADER | SEC_MENU_SENTENCE))
		dscore = 0.0;

	// . this is like 2+ links in the same sentence!
	// . need to fix first-avenue.com from getting that as a title...
	if ( si->m_flags & SEC_MENU_SENTENCE )
		tscore *= .50;

	// hurt it if not in title and in isolated link text too
	//if ( (si->m_flags & mask3 ) )
	//	dscore *= .05;
	
	if ( sflags & SENT_IN_MENU ) {
		//tscore *= .15;
		dscore = 0.0;
	}
	if ( sflags & SENT_IN_MENU_HEADER ) {
		tscore *= .16;
		dscore = 0.0;
	}

	// . same format of all items in menu
	// . hopefully will avoid hyperlinked event title followed by [map]
	// . fix SENT_IN_MENU algo if this isn't good enough
	if ( (sflags & SENT_IN_MENU) && (esflags & EVSENT_FORMAT_DUP) )
		tscore *= .15;

	// subsents should not be penalized for being in a word sandwhich.
	// fixes csulb.edu.
	if ( (sflags & SENT_WORD_SANDWICH) && ! isSubSent )
		tscore *= .04;
	if ( sflags & SENT_LOCATION_SANDWICH )
		tscore *= .04;
	if ( esflags & EVSENT_SECTIONDUP )
		tscore *= .50;
	if ( sflags & SENT_SECOND_TITLE ) {
		tscore *= .10;
		dscore *= .10;
	}
	if ( sflags & SENT_IN_HEADER )
		dscore *= 1.4;
	// this was messing up mhdancecenter.com which had widgettitle in
	// the tag, not really an event title. so lowered from 1.4 to 1.06.
	// at 1.02 residentadvisor preferred the title 1130pm-6am which is
	// bogus because 1130 is missing the colon and we think its not a time
	// . BUT for eventbrite.com xml feed <title> is the event title!!
	//   crap, actually its gbxmltitle because it was converted by us
	//   for internal collisions purposes. anyway, i saw one event where
	//   we got the better title in the description and not from the
	//   <gbxmltitle> tag of the eventbrite event!
	if ( (sflags & SENT_IN_TITLEY_TAG) && m_xd->m_contentType == CT_XML ) {
		tscore *= 5.0;
		dscore *= 5.0;
	}
	else if ( sflags & SENT_IN_TITLEY_TAG ) {
		tscore *= 1.06;//1.4;
		dscore *= 1.06;//1.4;
	}
	// this is like being in a tag too, right? actually a little better
	// because we are on our own line for sure...
	if ((sflags & SENT_AFTER_SPACER) && (sflags & SENT_BEFORE_SPACER)){
		tscore *= 1.01;
		dscore *= 1.01;
	}
	// this replaces being in a header tag
	else if ( sflags & SENT_IN_TAG ) {
		tscore *= 1.01;
		dscore *= 1.01;
	}

	if ( sflags & SENT_IN_TRUMBA_TITLE ) {
		tscore += 100.0;
		tscore *= 30.0;
		dscore *= 1.4;
	}
	if ( sflags & SENT_BAD_FIRST_WORD ) {
		tscore *= .75;
		dscore *= .75;
	}
	if ( sflags & SENT_NUKE_FIRST_WORD ) {
		tscore = 0.0;
		dscore = 0.0;
	}
	// was hurting a page that had <li>Tiberius<li>another band... so
	// i changed from .25 to .85
	if ( sflags & SENT_IN_LIST ) {
		tscore *= .85;
		dscore *= .85;
	}
	// unless also in a menu (sybarite.org)
	if ((si->m_flags & (SEC_MENU|SEC_MENU_HEADER|SEC_MENU_SENTENCE)) &&
	    (sflags & SENT_IN_LIST) ) {
		tscore *= .25;
		dscore *= .25;
	}
	// a big list is bad
	if ( sflags & SENT_IN_BIG_LIST ) {
		tscore *= .01;
		dscore *= .01;
	}
	
		
	// TODO: this used to punish based on the # of repeats
	// punish a little more for every repeat so that
	// "HEADLINER" will lose to "That 1 Guy" for
	// reverbnation.com.
	// this gets rid of "rod rogets dance company" for the
	// fabnyc.com url which is good.
	if ( sflags & SENT_PAGE_REPEAT )
		tscore *= .80;
	
	// SEC_MULT_EVENTS is sometimes set in two different places
	// above, so we might only have one event id bit set...
	if ( sflags & SENT_MULT_EVENTS )
		tscore *= .25;
	// now deal with the excess over 1
	long ned = si->m_numEventIdBits - 1;
	for ( ; ned > 0 ; ned-- ) tscore *= .85;
	
	if ( esflags & EVSENT_GENERIC_WORDS )
		// slam even more since a generic time sentence
		// was beating out the place name for an event with a
		// weekly schedule (DF_WEEKLY_SCHEDULE) event date,
		// for unm.edu. "3:30 pm. - 4 pm. Mon, Tues, 
		// Wed., Fri. (no Thurs.)"
		tscore *= .01; // .03;
	
	// fix guysndolllls.com bike night page11.html from using
	// the restaurant name as the title of biker's night
	if ( (sflags & SENT_OBVIOUS_PLACE) && ! isStoreHoursEvent ) 
		// setting this to .10 made some events that were using
		// the place name as the title, use a crappier title.
		// unm.edu instead of st. martin's hosp. center used
		// "shelter: xxxx" and sybarite.org used "audio & video"
		// clips from the menu instead of robertson recital hall.
		tscore *= 0.99;
		//tscore *= .10;

	// . fix otherchangeofhobbit.com to use place name as title
	// . awesome! this fixed yelp.com studio gallery title,
	//   rialtopoolroom.com and bacchussecretcellar.com
	if ( (sflags & SENT_OBVIOUS_PLACE) &&   isStoreHoursEvent ) 
		tscore *= 1.10;
	
	if ( esflags & EVSENT_GENERIC_PLUS_PLACE ) {
		if ( isStoreHoursEvent ) tscore *= 1.10;
		else                     tscore *= 0.90;
	}
	
	if ( sflags & SENT_PLACE_NAME ) {
		if ( isStoreHoursEvent ) tscore *= 3.0;//1.10;
		// but if the date is recurring it might be a "free" day
		// at the museum like on collectorsguide.com for the abq
		// museum of art and natural history. crap but for denver.org
		// this makes the place name win over 'Art Goes to the Movies'
		// because date has 'Thursdays', so just punish .50 then.
		else if ( ev->m_date->m_suppFlags & SF_RECURRING_DOW ) 
			tscore *= .5;
		// prefer format dup over place name...
		// put this back temporarily to fix <th> on evie says
		// i had to change from .90 to .10 here because
		// zvent's oppenheimer page was prefering the 
		// "The Filling Station" because the actual title
		// had too high a penalty from MULT_EVENTS...
		// perhaps though since all the dates are adjacent
		// the multevents penalty should not be so high!
		else                     tscore *= 0.10;
	}

	// if we are store hours and the sentence contains the
	// place name, give it a slight boost...
	if ( sflags & SENT_CONTAINS_PLACE_NAME )
		if ( isStoreHoursEvent ) tscore *= 1.01;
	
	
	// . zvents.com has "Events Venues Restaurants Movies Performers"@A=120
	// . i think there are some events with titles in a link and then
	//   another link like [map] or something, so let's add the onrootpage
	//   flag here too as well
	//if((si->m_flags & SEC_MENU_SENTENCE) && (sflags & SENT_ONROOTPAGE)){
	//	tscore *= .20;
	//	dscore *= .20;
	//}

	// shortcut
	sentflags_t menu=(SENT_IN_MENU|SENT_IN_MENU_HEADER|SENT_MENU_SENTENCE);

	// in menu and also on root page in addition to this page, punish!
	//if ( (sflags & SENT_ONROOTPAGE) && (sflags & menu) ) {
	if ( si->m_votesForDup > si->m_votesForNotDup && (sflags & menu) ) {
		tscore *= .05;
		dscore *= .05;
	}

	// zero out if its a lot!
	// no longer require the menu bits!!
	// same logic as EDF_MENU_CRUFT!!!
	//if ( sflags & SENT_DUP_SECTION ) {
	if ( esflags & EVSENT_SECTIONDUP ) {
		tscore = 0.0;
		dscore = 0.0;
	}

	//bool dateOnRootPage = false;
	//Section *ds = ev->m_date->m_section;
	//if ( ds ) ds = ds->m_sentenceSection;
	//if ( ds && (ds->m_sentFlags & SENT_ONROOTPAGE) ) 
	//	dateOnRootPage = true;
	// . slightly punish if on root page
	// . try to fix dukecityfix.com from having website title as title
	// . if the date is not on the root page then event title should not
	//   be on the root page... most likely...
	// . no, hurts metroplisarts.com
	//if ( (sflags & SENT_ONROOTPAGE) && ! dateOnRootPage ) 
	//	tscore *= .999;

	// if we have mult events set AND in menu, then give a little
	// extra penalty, because it is more likely really in a menu.
	// this fixes unm.edu so it doesn't get "Index" as a title
	// and gets the title tag of the page instead, and this keeps
	// southgatehouse.com from keeping its menu title, which is
	// the correct one and it does not have SENT_MULT_EVENTS set 
	// the unm.edu url does, so it is unaffected by this.
	if ( (sflags & SENT_MULT_EVENTS) && (sflags & menu) ){
		tscore *= .89;
		dscore *= .89;
	}
	// and this fixes "Lyrics" from being a title for
	// reverbnation.com... but hurts southgatehouse.com which
	// repeats the band name in another section for some reason...
	if ( (sflags & SENT_PAGE_REPEAT) && (sflags & menu) ) {
		tscore *= .30;
		dscore *= .30;
	}
	// anticipate this...
	if ( (esflags & EVSENT_SECTIONDUP) && (sflags & menu) ) {
		tscore *= .20;
		dscore *= .20;
	}
	// really a hack for unm.edu which has 
	// "go to the top of the page" same page link
	if ( (sflags & SENT_PAGE_REPEAT) && 
	     (sflags & menu) &&
	     (sflags & SENT_MIXED_CASE) ) {
		tscore = 0.0;
		dscore = 0.0;
	}
	if ( (sflags & SENT_MULT_EVENTS) && 
	     (sflags & menu) &&
	     (sflags & SENT_MIXED_CASE) ) {
		tscore = 0.0;
		dscore = 0.0;
	}

	// if you are in the same sentence as part of the event date
	// give a bonus
	if ( sflags & SENT_HASSOMEEVENTSDATE ) 
		tscore *= 1.01;


	// button tags are like input tags
	if ( si->m_next && 
	     si->m_next->m_a == si->m_a &&
	     si->m_next->m_tagId == TAG_BUTTON ) {
		tscore = 0.0;
		dscore = 0.0;
	}

#ifdef _USETURKS_
	/*
	// make the key like in XmlDoc::getTurkVotingTable()
	uint64_t key = (((uint64_t)si)<<32) | (uint32_t)ev;
	// get its turkbits
	turkbits_t *tbp = (turkbits_t *)m_tbt->getValue ( &key );
	// shortcut
	turkbits_t tb = 0;
	// assign if we had them
	if ( tbp ) {
		tb = *tbp;
		// sanity
		// . no, this can happen if in XmlDoc::getTurkBitsTable()
		//   it removes a title bit becaise it ended up having
		//   a maxScore title, so rather than have multiple based
		//   on tag hash votes, we pick a single max title based
		//   on content hash votes for just that event/page
		//   titles, we pick the single
		//if ( tb == 0 ) { char *xx=NULL;*xx=0; }
	}

	// a hack to debug i-title-1856930530-25143599
	//if ( si->m_tagHash == 1856930530 &&
	//     si->m_contentHash == 251435990 )
	//	tb |= TB_TITLE;

	// . mod dscore based on turk votes
	// . maybe the tag hash was voted to not be part of desc...
	if ( tb & TB_DESCR )
		dscore = 100.0;
	// exclude tag overrides
	if ( tb & TB_NOT_DESCR )
		dscore = 0.0;
	*/
#endif

	// might as well set this here
	//si->m_descScore = dscore;
	// really just use this now
	if ( retDescScore ) *retDescScore = dscore;


	if ( esflags & EVSENT_CLOSETODATE ) 
		tscore *= 1.05;

#ifdef _USETURKS_
	/*
	///////////////////
	//
	// apply turk voting
	//
	///////////////////

	// if turks seemd to prefer one title, give bonus...
	if ( tb & TB_TITLE ) 
		tscore += 500.0;
	*/
#endif

	float factor = si->getSectiondbVoteFactor();
	tscore *= factor;
	dscore *= factor;

	
	// if the tag was flagged by a turk based on taghash and should not
	// be part of any event description on this site... punish a lot
	//if ( tb & TB_BAD_DESC_TAG )
	//	tscore *= 0.10;

	// update this
	//*extraFlags = sflags;

	// return it
	return tscore;
}

void addEventIds ( Section *dst , Section *src ) {
	// if no events, bail
	if ( src->m_minEventId <= 0 ) return;
	// quick sets
	if ( dst->m_minEventId <= 0 ) {
		dst->m_minEventId = src->m_minEventId;
		dst->m_maxEventId = src->m_maxEventId;
	}
	// get better of the two
	if ( src->m_minEventId < dst->m_minEventId )
		dst->m_minEventId = src->m_minEventId;
	if ( src->m_maxEventId > dst->m_maxEventId )
		dst->m_maxEventId = src->m_maxEventId;
	// reset this
	dst->m_numEventIdBits = 0;
	// then the bits
	for ( long i = 0 ; i < 32 ; i++ ) {
		dst->m_evIdBits[i] |= src->m_evIdBits[i];
		// re-do the count
		dst->m_numEventIdBits += getNumBitsOn8(dst->m_evIdBits[i]);
	}
	// sanity check
	if ( dst->m_numEventIdBits < src->m_numEventIdBits ) { 
		char *xx=NULL;*xx=0;}
}

// . returns false and sets g_errno on error
// . sets Event "ev"'s m_intervalsOff to the offset of our array of Intervals
//   in "sb" which corrspond to the time ranges of the event.
// . Event::m_ni is how many Intervals there are
bool Events::getIntervals ( Event *ev , SafeBuf *sb ) {

	// offset to start of safebuf
	ev->m_intervalsOff = sb->length();

	//if ( sb->length() == 1752 )
	//	log("hey");

	// now each "event" is a single TOD (or TOD range or list),
	// or basically a single date that contains a TOD
	Date *di = ev->m_date;

	Interval *int3  = NULL;
	bool allDone    = false;
	//bool isFacebook = (di->m_flags & DF_FACEBOOK);

	// support for facebook timestamps, already in UTC
	if ( di->m_hasType == (DT_TIMESTAMP|DT_COMPOUND) ) {
		Date *dp = di->m_ptrs[0];
		ev->m_maxStartTime = dp->m_num;
		ev->m_ni = 1;
		if ( ! m_sb.reserve ( sizeof(Interval) ) ) return false;
		int3 = (Interval *)(ev->m_intervalsOff + m_sb.getBufStart());
		int3->m_a = dp->m_num;//timestamp
		int3->m_b = dp->m_num;//timestamp;
		m_sb.m_length += sizeof(Interval);
		allDone = true;
	}
	// facebook timestamp ranges <start_time> --> <end_time>
	if ( di->m_hasType == (DT_TIMESTAMP|DT_COMPOUND|DT_RANGE) ) {
		Date *dp1 = di->m_ptrs[0];
		Date *dp2 = di->m_ptrs[1];
		ev->m_maxStartTime = dp1->m_num;
		ev->m_ni = 1;
		if ( ! m_sb.reserve ( sizeof(Interval) ) ) return false;
		int3 = (Interval *)(ev->m_intervalsOff + m_sb.getBufStart());
		int3->m_a = dp1->m_num;//timestamp;
		int3->m_b = dp2->m_num;//timestamp;
		m_sb.m_length += sizeof(Interval);
		allDone = true;
	}
	// dumbass facebook assumed all times were in california
	// when converting into utc so we have to get timezone.
	// but facebook's json feed uses trumba style date formatting not
	// unix timestamps, so i'd imagine it might be correct. so only
	// do this for DT_TIMESTAMP i guess.
	if ( allDone && m_isFacebook && (di->m_hasType & DT_TIMESTAMP) ) {
		char useDST;
		char tz = ev->m_address->getTimeZone(&useDST);
		char california = -8;
		// get differents
		long diff = california - tz ;
		// and adjust
		int3->m_a += 3600 * diff;
		int3->m_b += 3600 * diff;
		ev->m_maxStartTime += 3600 * diff;
	}

	if ( allDone )
		return true;

	// see if we added any intervals
	long sblen1 = sb->length();
	// does the event's city acknowledge DST?
	char useDST;
	char timeZone = ev->m_address->getTimeZone ( &useDST );

	// if unknown timezone, bail
	if ( timeZone == UNKNOWN_TIMEZONE ) {
		// log it for prosterity
		char city[64];
		char state[32];
		city[0] = 0;
		state[0] = 9;
		Place *c = ev->m_address->m_city;
		if ( c && c->m_str && c->m_strlen < 60 ) {
			memcpy ( city,c->m_str,c->m_strlen);
			city[c->m_strlen] = 0;
		}
		Place *a = ev->m_address->m_adm1;
		if ( a && a->m_adm1 ) {
			state[0] = a->m_adm1[0];
			state[1] = a->m_adm1[1];
			state[2] = 0;
		}
		log("events: had unknown timezone for event city=%s state=%s",
		    city,state);
		// set this to zero so our caller knows intervals are empty
		ev->m_maxStartTime = 0;
		ev->m_ni           = 0;
		return true;
	}

	// . ok, hash all points in time this date represents
	// . i.e. "every wednesday", "8pm every weekday", etc.
	// . this now returns all time intervals in UTC (non-DST), so if
	//   the event is always at 8am in a DST ackowledging county then
	//   it will have UTC dates whose tod changes depending on if DST
	//   is in effect or not. someone in arizona would see a tod
	//   perturbation in wait time change of an hour between weekly 
	//   recurring times for an event in new mexico for example.
	if ( ! m_dates->getIntervals2 ( di,
					sb,
					m_year0,
					m_year1,
					ev->m_closeDates,
					ev->m_numCloseDates ,
					timeZone ,
					useDST ,
					m_words ) )
		return false;
	// see if we added any intervals
	long sblen2 = sb->length();
	// skip if no dates added!
	if ( sblen1 == sblen2 ) return true;
	// otherwise, flag it as good!
	//di->m_flags |= DF_GOOD_EVENT_DATE;

	// the ending offset
	long eoff = sb->length();
	// get it
	long ni = (eoff - ev->m_intervalsOff)/ sizeof(Interval);
	// set the # of time_t intervals we had for this event
	ev->m_ni = ni;

	// shortcuts to the list of intervals this event has
	int3 = (Interval *)(ev->m_intervalsOff + m_sb.getBufStart());

	// MDW: this is now done in Dates::getIntervals2()
	// we added the intervals using mktime_utc() so its as if the
	// event occurred in Greenwhich, England (UTC+0) but that is not
	// the case
	//long tz = 0;

	// . but if one event is at 2pm in calif and another event is at
	//   2pm in DC, then we need to sort them by when they really occur
	// . getTimeZone() returns -7 for abq NM, no DST, -8 for Calif.
	// . so we have to advance the time_t points more for an event in
	//   California than for an event in New Mexico sinc they are in
	//   different timezones.
	// . this properly converts the event times into UTC
	//tz += 3600 * ev->m_address->getTimeZone ( );

	// are we store hours?
	//bool storeHours = true;
	// not if no end point for the tod range...
	//if ( ni > 0 && int3[0].m_b == int3[0].m_a ) storeHours = false;

	// . convert all time points to UTC
	// . apply the timezone mod
	// . so when we index them in datedb we can constrain and sort by
	//   when the events actually start independent of time zone
	// . addIntervals() ignores DST, so we can ignore here as well
	//for ( long i = 0 ; i < ni ; i++ ) {
	//	// breathe
	//	QUICKPOLL(m_niceness);
	//	// switch to UTC
	//	int3[i].m_a -= tz;
	//	int3[i].m_b -= tz;
	//}


	//
	// set ev->m_maxStartTime now
	//

	// reset
	time_t max = 0;
	// scan for max
	for ( long i = 0 ; i < ni ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// extract the left endpoint time (in seconds since epoch)
		time_t date = int3[i].m_a;
		// compare
		if ( date > max ) max = date;
	}
	// set the max start time
	ev->m_maxStartTime = max;

	//bool storeHours = (ev->m_flags & EV_STORE_HOURS);
	// set low bit of end time to indicate if store hours or not
	//for ( long i = 0 ; i < ni ; i++ ) {
	//	// breathe
	//	QUICKPOLL(m_niceness);
	//	// . are store hours?
	//	// . set bit 0 on the date2 if we are store hours
	//	if ( storeHours ) int3[i].m_b |=  0x01;
	//	else              int3[i].m_b &= ~0x01;
	//}

	// success
	return true;
}


bool Events::print ( SafeBuf *pbuf , long siteHash32 , long long uh64 ) {

	if ( ! pbuf ) return true;

	char *hdrFormat = 
		"<table cellpadding=3 border=1>"
		"<tr>"
		"<td colspan=40>"
		// table header row:
		"%s"
		"</td>"
		"</tr>"
		"<tr>"
		"<td><b>#</b></td>"
		"<td><b>evId</b></td>"
		"<td><b>evId2</b></td>"
		"<td><b>evstorehrs</b></td>"
		"<td><b><nobr>title start</nobr></b></td>"
		"<td><b><nobr>evflags</nobr></b></td>"
		"<td><b><nobr>maxStartTime"

		"%s"

		"</nobr></b></td>"
		"<td><b><nobr>event title</nobr></b></td>"
		"<td><b><nobr>event hash</nobr></b></td>"

		"<td><b><nobr>date content hash</nobr></b></td>"
		"<td><b><nobr>addr content hash</nobr></b></td>"
		"<td><b><nobr>date type/tag hash</nobr></b></td>"
		"<td><b><nobr>addr tag hash</nobr></b></td>"
		"<td><b><nobr>adth32</nobr></b></td>"
		"<td><b><nobr>adch32</nobr></b></td>"

		"<td><b>base date</b></td>"
		"<td><b>norm date</b></td>"
		//"<td><b>times</b></td>"
		"<td><b><nobr>place name 1</nobr></b></td>"
		"<td><b><nobr>place name 2</nobr></b></td>"
		"<td><b>suite</b></td>"
		"<td><b>street</b></td>"
		"<td><b>city</b></td>"
		"<td><b>adm1</b></td>"
		"<td><b>zip</b></td>"
		"<td><b>ctry</b></td>"

		"<td><b>latitude</b></td>"
		"<td><b>longitude</b></td>"

		"<td><nobr>--------<b>desc1</b>--------</nobr></td>"
		//"<td><nobr>--------<b>desc2</b>--------</nobr></td>"
		//"<td>tags</td>"
		"</tr>\n" ;

	// Spider.cpp when storing parse.* file will also store an
	// abbreviate file called parse-shortdisplay.* consisting only
	// of these div tags for rendering within the qa.html file! that
	// way the qa person can easily check/uncheck all the checkboxes
	// right in the qa.html file
	pbuf->safePrintf("<div class=shortdisplay>\n");

	// define checkall function
	pbuf->safePrintf (  "<!--ignore-->"
			    "<script type=\"text/javascript\">"
			    "function checkAll(form, name, num) { "
			    "    for (var i = 0; i < num; i++) {"
			    "      var nombre;"
			    "      nombre = name + i;"
			    "      var e = document.getElementById(nombre);"
			    "      if ( e == null ) {continue;}"
			    "      e.checked = !e.checked;"
			    "      e.onclick();"
			    "    }"
			    "}"
			    "</script>");
	
	SafeBuf all;
	all.safePrintf( " (<input type=checkbox onclick=\"" );
	all.safePrintf("checkAll(this, 'cb', %li);", m_numEvents );
	all.safePrintf( "\">toggle all</a>)"  );


	// print checkbox to indicate if events are wrong
	pbuf->safePrintf ( "<!--ignore-->" // ignore for Test.cpp diff
			   "<br>"
			   "<nobr>"
			   // light blue background
			   "<span class=validated "
			   "style=background-color:#9090e0>"
			   "<input type=checkbox "
			   "onclick=\"senddiv(this,'%lli');\" "
			   "unchecked>"
			   "<div class=validated style=display:inline>"
			   " Has <b>event</b> parsing issue. Flag to fix."
			   "</div>"
			   "</span>"
			   "</nobr>" 
			   "<br>"
			   "<br>\n" ,
			   uh64 );

	long validCount = 0;
	// print out the VALID events
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// skip if invalid
		//if ( ev->m_flags & EV_BAD_EVENT ) continue;
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		// count as valid
		validCount++;
	}

	// spidered time
	//char stbuf[128];
	//struct tm *timeStruct = localtime ( &m_spideredTime);
	//strftime(stbuf,100,"%b %d, %Y %H:%M:%S",timeStruct);
	//if ( m_spideredTime == 0 ) sprintf(stbuf,"---");

	// add in the # of valid events, this will be a validated div
	// tag so that XmlDoc::validateOutput() can confirm it
	SafeBuf tmp;

	tmp.safePrintf ( "<!--ignore-->" // ignore for Test.cpp diff
			 "<nobr>"
			 "<span class=validated>"
			 "<input type=checkbox "
			 "onclick=\"senddiv(this,'%lli');\" "
			 "unchecked>"
			 "<div class=validated style=display:inline>"
			 " %li Valid Events"
			 "</div>"
			 "</span>\n"
			 " (spideredtime=%s UTC)"
			 "</nobr>" ,
			 // must be unsigned
			 uh64 , 
			 validCount ,
			 //stbuf);
			 asctime(gmtime ( &m_spideredTime )) );

	pbuf->safePrintf ( hdrFormat , tmp.getBufStart(), all.getBufStart() );

	// print out the VALID events
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		//if ( ev->m_flags & EV_BAD_EVENT ) continue;
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		printEvent ( pbuf , i , uh64 );
	}

	// sanity
	//if ( validCount != m_numValidEvents ) { char *xx=NULL;*xx=0; }


	pbuf->safePrintf("</table>\n");
	pbuf->safePrintf("</div class=shortdisplay>\n");//end shortdisplay div
	pbuf->safePrintf("<br>\n");

	if ( m_note )
		pbuf->safePrintf("%s<br>",m_note);

	//
	// now print invalid events
	//

	pbuf->safePrintf ( hdrFormat , "INvalid Events" , "" );

	// print out the INVALID events
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		//if ( ev->m_flags & EV_BAD_EVENT ) valid = false;
		if ( ! (ev->m_flags & EV_DO_NOT_INDEX ) ) continue;
		printEvent ( pbuf , i , 0 );
	}

	pbuf->safePrintf("</table>\n");
	pbuf->safePrintf("<br>\n");

	return true;
}

// returns false with g_errno set on error
bool printEventDisplays ( SafeBuf *sb ,
			  long numHashableEvents ,
			  char *ptr_eventData ,
			  long size_eventData ,
			  //char *ptr_eventTagBuf ,
			  char *ptr_utf8Content ) {

	if ( numHashableEvents <= 0 ) return true;

	// print table header
	if ( ! sb->safePrintf ( "<table cellpadding=3 border=1 "
				"bgcolor=lightblue>"
				"<tr>"
				"<td colspan=50>"
				"FINAL Event Displays stored in TitleRec"
				"</td>"
				"</tr>\n"
				"<tr>"
				"<td><b>indxEvId</b></td>"
				"<td><b>eventFlags</b></td>"
				"<td><b>confirmFlags</b></td>"
				
				"<td><b>eventhash64</b></td>"
				"<td><b>dateContentHash64</b></td>"
				"<td><b>addrContentHash64</b></td>"
				"<td><b>dateTagHash32</b></td>"
				"<td><b>addrTagHash32</b></td>"
				"<td><b>titleTagHash32</b></td>"
				
				"<td><b>adch32</b></td>"
				"<td><b>adth32</b></td>"
				
				// m_numDescriptions/Descriptors (# EventDesc)
				"<td><b>nd</b></td>"
				
				"<td><b>address</b></td>"
				"<td><b>normdate</b></td>"
				"<td><b>geocoderLat</b></td>"
				"<td><b>geocoderLon</b></td>"
				"</tr>\n" ))
		return false;



	EventDisplay *ed;
	for ( long i = 1 ; i <= numHashableEvents ; i++ ) {
		ed = ::getEventDisplay (i,
					ptr_eventData,
					size_eventData);
					//ptr_eventTagBuf );
		ed->printEventDisplay ( sb , ptr_utf8Content );
	}

	// print table closer
	if ( ! sb->safePrintf("</table>\n<br>\n" ) ) return false;

	return true;
}
			  
bool EventDisplay::printEventDisplay ( SafeBuf *sb , char *ptr_utf8Content ) {

	if ( ! sb->safePrintf ( "<tr>"
				"<td>%li</td>"
				"<td>"
				,m_indexedEventId ) )
		return false;

	if ( ! printEventFlags ( sb , m_eventFlags ) )
		return false;

	if ( ! sb->safePrintf ( "</td><td>" ) ) return false;

	if ( ! printConfirmFlags ( sb , m_confirmed ) ) return false;

	if ( ! sb->safePrintf ( "</td>"
				"<td>%llu</td>"
				"<td>%llu</td>"
				"<td>%llu</td>"
				"<td>%lu</td>"
				"<td>%lu</td>"
				"<td>%lu</td>" // titletaghash32
				"<td>%lu</td>" // adch32
				"<td>%lu</td>" // adth32
				"<td>%li</td>" // nd
				"<td><nobr>%s</nobr></td>" 
				"<td><nobr>%s</nobr></td>" 
				"<td>%.05f</td>" 
				"<td>%.05f</td>" 
				"<td><nobr>\n"
				,m_eventHash64
				,m_dateHash64
				,m_addressHash64
				,m_dateTagHash32
				,m_addressTagHash32
				,m_titleTagHash32
				,m_adch32
				,m_adth32
				,m_numDescriptions
				,m_addr // text
				,m_normDate // text
				,m_geocoderLat
				,m_geocoderLon ))
		return false;

	// now print each EventDesc
	for ( long i = 0 ; i < m_numDescriptions ; i++ ) {
		EventDesc *ec = &m_desc[i];

		if ( ! ec->printEventDesc( sb , ptr_utf8Content ) )
			return false;

		// separator
		if ( i < m_numDescriptions -1 )
			if ( ! sb->safePrintf(" [[]] " ) ) return false;
	}

	// end row
	if ( ! sb->safePrintf("</nobr></td></tr>\n")) return false;

	return true;
}

bool EventDesc::printEventDesc ( SafeBuf *sb , char *ptr_utf8Content ) {

	// make string
	char *str = ptr_utf8Content + m_off1;
	char *end = ptr_utf8Content + m_off2;
	long len = end - str;
	// set its xml class to remove tags
	char c = str[len];
	str[len] = '\0';
	long version = TITLEREC_CURRENT_VERSION;
	Xml xml;
	xml.set(str,len,false,0,false,version);
	str[len] = c;
	// set words class
	Words w; 
	w.set ( &xml , true );//str , len , 0 ); // niceness = 0
	Bits bits;
	bits.set (&w , version , 0);//niceness ) ;
	Phrases phrases;
	phrases.set(&w,&bits,true,false,version,0);//niceness );
	// shortcuts
	nodeid_t   *tids  = w.getTagIds();
	char      **wptrs = w.getWords();
	long       *wlens = w.getWordLens();
	long long  *wids  = w.getWordIds();
	// rese this for words
	bool lastWasPunct = true;

	if ( m_dflags & EDF_INDEXABLE )
		if ( ! sb->safePrintf("<b>" ) ) return false;

	// print out each word and each words synonyms
	for ( long i = 0 ; i < w.m_numWords ; i++ ) {
		// skip tags
		if ( tids && tids[i] ) continue;
		// if punct, print just first char
		if ( ! wids[i] ) {
			lastWasPunct = true;
		}
		else {
			if ( ! lastWasPunct )
				if ( ! sb->pushChar(' ') ) return false;
			lastWasPunct = false;
		}
		// print the word
		if ( ! sb->safeMemcpy(wptrs[i],wlens[i]) ) return false;
	}

	if ( m_dflags & EDF_INDEXABLE )
		if ( ! sb->safePrintf("</b>" ) ) return false;

	// now print the flags in subcase
	if ( ! sb->safePrintf("<sub><font color=red>") ) return false;
	if ( ! printEventDescFlags ( sb, m_dflags ) ) return false;
	if ( ! sb->safePrintf("</font></sub>") ) return false;
	
	// add a \0 at the end
	//sb->pushChar('\0');
	return true;
}


bool Events::printEvent ( SafeBuf *pbuf , long i , long long uh64 ) {

	// shortcut
	Words *ww = m_words;

	// get ith event
	Event *ev = &m_events[i];
	
	bool validEvent = true;
	if ( ev->m_flags & EV_BAD_EVENT ) validEvent=false;
	//if ( ev->m_flags & EV_OLD_EVENT ) validEvent=false;

	evflags_t evf = ev->m_flags;

	// mask out store hours bit
	evf &= ~EV_STORE_HOURS;
	evf &= ~EV_SUBSTORE_HOURS;
	evf &= ~EV_HASTITLEWORDS;
	evf &= ~EV_HASTITLEFIELD;
	evf &= ~EV_HASTITLESUBSENT;
	evf &= ~EV_HASTITLEBYVOTES;
	evf &= ~EV_HASTITLEWITHCOLON;
	evf &= ~EV_HASTIGHTDATE;
	evf &= ~EV_INCRAZYTABLE;
	evf &= ~EV_LONGDURATION;
	evf &= ~EV_PRIVATE;
	evf &= ~EV_FACEBOOK;
	evf &= ~EV_STUBHUB;
	evf &= ~EV_EVENTBRITE;

	// consider any of these invalid i guess
	if ( evf ) validEvent = false;
	
	// skip if not event any more
	//if ( ! ( ev->m_section->m_flags & SEC_EVENT ) ) continue;
	char *attr1 = "<b>";
	char *attr2 = "</b>";
	char *bg = "#00c000";
	if ( ! validEvent ) {
		attr1 = "";//<strike>";
		attr2 = "";//</strike>";
		bg = "#ffffff";
	}
	// show it
	long a = ev->m_titleStart;
	long b = ev->m_titleEnd;
	// print # and title start #
	pbuf->safePrintf("<tr bgcolor=%s>\n"
			 "<!--ignore--><td>%li</td>\n",bg,i);
	
	if ( ev->m_eventId >= 1 )
		pbuf->safePrintf("<td><b>%li</b></td>",
				 (long)ev->m_eventId);
	else
		pbuf->safePrintf("<td>&nbsp</td>");

	// saves on having to allocate too many slots for indextable2
	// intersection algo if we are including "blank" event ids, so we
	// now use m_indexedEventId for indexing purposes
	if ( ev->m_indexedEventId >= 1 )
		pbuf->safePrintf("<td><b>%li</b></td>",
				 (long)ev->m_indexedEventId);
	else
		pbuf->safePrintf("<td>&nbsp</td>");


	// store horus?
	if ( ev->m_flags & EV_STORE_HOURS )
	       pbuf->safePrintf("<td><font color=yellow><b>Y</b></font></td>");
	else if ( ev->m_date->m_flags & DF_WEEKLY_SCHEDULE )
		pbuf->safePrintf("<td>W</td>");
	else
		pbuf->safePrintf("<td>N</td>");
	
	// title start
	pbuf->safePrintf("<td>%li</td>",a);
	
	// shortcut
	//sec_t sf = ev->m_section->m_flags;
	// print flags
	pbuf->safePrintf("<td><nobr>");
	// make sure we wrote something
	//long blen1 = pbuf->length();

	pbuf->safePrintf("nd=%li ",ev->m_numDescriptions);

	printEventFlags ( pbuf , ev->m_flags );

	m_sections->printFlags ( pbuf , ev->m_section , true );

	//long blen2 = pbuf->length();
	// if not an event, and did not write anything, core
	//if ( ! (sf & SEC_EVENT) && blen1 == blen2){char*xx=NULL;*xx=0;}
	pbuf->safePrintf("</nobr></td>");
	
	// event max start time
	char tbuf[128];
	struct tm *timeStruct = localtime ( &ev->m_maxStartTime );
	strftime(tbuf,100,"%b %d, %Y %H:%M:%S",timeStruct);
	if ( ev->m_maxStartTime == 0 ) sprintf(tbuf,"---");


	Address *addr = ev->m_address;
	
	pbuf->safePrintf("<td>");
	// start title
	pbuf->safePrintf("<nobr>");

	// . this is for XmlDoc::validateOutput()
	// . put everything you want to validate in here for this event
	if ( uh64 ) {
		printEventForCheckbox(ev,pbuf,uh64,i,"cb");
	}
	
	// print max start time
	pbuf->safePrintf("%s%s%s (%li)</nobr></td>",
			 attr1,
			 tbuf,
			 attr2,
			 ev->m_maxStartTime);
	
	pbuf->safePrintf("<td>");
	// start title
	pbuf->safePrintf("<nobr>");

	pbuf->safePrintf("%s",attr1);
	// get string
	//char *s = ww->m_words[a];
	//char *e = s;
	// sometimes our titles are empty!! now that we no longer
	// require a title since we may have a list of time
	// sections (with a little cruft in some)
	//if ( b != a && b > a ) e=ww->m_words[b-1]+ww->m_wordLens[b-1];

	bool lastPunct = true;
	// print out each word one at a time so we can remove tags
	for ( long i = a ; i < b ; i++ ) {
		// skip tags
		if ( m_tids[i] ) { 
			if ( ! lastPunct )pbuf->safeMemcpy ( " * ", 3);
			lastPunct = true;
			continue; 
		}
		// record if punct or alnum
		if ( m_wids[i] ) lastPunct = false;
		else             lastPunct = true;
		// otherwise print it
		pbuf->safeMemcpy ( m_wptrs[i], m_wlens[i] );
	}

	// the title
	//pbuf->safeMemcpy( s , e -s );
	pbuf->safePrintf("%s",attr2);
	pbuf->safePrintf("</nobr></td>");
	
	// section tag hash
	//pbuf->safePrintf("<td>0x%lx</td>",(long)ev->m_section->m_tagHash);
	pbuf->safePrintf("<td>%llu</td>",ev->m_eventHash64);
	
	pbuf->safePrintf("<td>%llu</td>",ev->m_dateHash64);
	pbuf->safePrintf("<td>%llu</td>",ev->m_addressHash64);
	pbuf->safePrintf("<td>%lu</td>",ev->m_dateTagHash32);
	pbuf->safePrintf("<td>%lu</td>",ev->m_addressTagHash32);
	pbuf->safePrintf("<td>%lu</td>",ev->m_adth32);
	pbuf->safePrintf("<td>%lu</td>",ev->m_adch32);


	// short cut
	//long fdn = ev->m_firstDatePtrNum;
	// sanity check
	//if (fdn<0 ||fdn>=m_dates->m_numDatePtrs) {char *xx=NULL;*xx=0;}
	// loop over all dates in our section
	
	
	char last = 0;
	// frame it
	pbuf->safePrintf("<td><nobr>");
	// reset count
	long dcount = 0;
	// start scanning at the first Date in this event's section
	//for ( long i = ev->m_firstDatePtrNum ; 
	//      i < m_dates->m_numDatePtrs ; i++ ) {
	for ( long i = 0 ; i < m_dates->m_numDatePtrs ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = m_dates->m_datePtrs[i];
		// skip if replaced
		if ( ! di ) continue;
		// skip if a close date
		if ( di->m_flags & DF_CLOSE_DATE ) continue;
		// skip if before section
		if ( di->m_a <  ev->m_section->m_a ) continue;
		// stop if breached. need to continue now that one date
		// can have multiple telescopes...
		if ( di->m_a >= ev->m_section->m_b ) continue;//break;
		// if we hit the telescoped dates section then
		// word #'s wrap around, so stop on that
		//if ( di->m_b <  ev->m_section->m_a ) break;
		//if ( di->m_type == DT_TELESCOPE ) break;

		Date *dp = di;
		// grab telescope if we should
		//if ( dp->m_telescope ) continue;//dp = dp->m_telescope;
		
		char *attr1 = "";
		char *attr2 = "";
		// skip if no good
		// only boldify the selected event date
		if ( dp == ev->m_date ) {
			//if ( di->m_flags & DF_GOOD_EVENT_DATE ) {
			attr1 = "<b>";
			attr2 = "</b>";
		}

		// why are we printing this form of the date now???
		if ( dp != ev->m_date ) continue;
		
		// is also part of verified/inlined address?
		//if ( m_bits[di->m_a] & D_IS_IN_ADDRESS ) continue;
		// if one before break it
		if ( last ) pbuf->safeMemcpy(" ... ",5);
		// set it
		last = 1;
		
		pbuf->safePrintf("%s",attr1);

		// print it
		dp->printText ( pbuf , ww );
		
		pbuf->safePrintf("%s",attr2);
		
		// count it
		dcount++;
		
		// stop after 2 dates if we are not valid section
		if ( dcount < 6     ) continue;
		//if ( (sf & SEC_EVENT) && !(sf & SEC_BAD_EVENT) ) 
		//	continue;
		// keep going if valid
		if ( ev->m_flags & EV_BAD_EVENT ) {
			pbuf->safePrintf("...CUT");
			break;
		}
	}







	// the dates when we are closed
	for ( long i = 0 ; i < ev->m_numCloseDates ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get it
		Date *di = ev->m_closeDates[i];
		pbuf->safePrintf(" <b>(");
		// print it
		di->printText ( pbuf , ww );
		pbuf->safePrintf(")</b>");
	}

	// frame it
	pbuf->safePrintf("</nobr></td>");

	// the normalized date
	pbuf->safePrintf("<td><nobr>");	
	ev->m_date->printTextNorm ( pbuf , m_words, true , ev , &m_sb );
	pbuf->safePrintf("</nobr></td>");	
	
	//char tbuf[128];
	//struct tm *timeStruct = localtime ( &ev->m_startTime );
	//strftime(tbuf,100,"%b %d, %Y %H:%M:%S",timeStruct);
	// close tag and print start time end empty end time
	//pbuf->safePrintf("<td><nobr>%s</nobr></td>",tbuf);
	
	/*
	  pbuf->safePrintf("<td>");
	  // print out the TODs
	  for ( long k = 0 ; k < ev->m_nd ; k++ ) {
	  if ( k > 0 ) pbuf->safePrintf("<br>");
	  // do not break it
	  pbuf->safePrintf("<nobr>");
	  // get the date
	  Date *dd = ev->m_dates[k];
	  // print it out
	  dd->print ( pbuf            , 
	  m_sections      ,
	  m_words         ,
	  siteHash32      ,
	  k               ,
	  m_dates->m_best );
	  // do not break it
	  pbuf->safePrintf("</nobr>");
	  }
	  pbuf->safePrintf("</td>");
	*/
	
	//if ( ! addr ) { char *xx=NULL;*xx=0; }

	// use this now
	if ( addr ) addr->printEssentials ( pbuf , true , 0LL );

	/*
	// place name 1
	long len = 0;
	if ( addr &&
	     addr->m_name1.m_str &&
	     // only print if verified!
	     addr->m_flags & AF_VERIFIED_PLACE_NAME_1 ) { 
		s   = addr->m_name1.m_str;
		len = addr->m_name1.m_strlen;
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy( s , len );
	pbuf->safePrintf("</nobr></td>");
	
	// place name 2
	len = 0;
	if ( addr &&
	     addr->m_name2.m_str &&
	     // only print if verified!
	     addr->m_flags & AF_VERIFIED_PLACE_NAME_2 ) { 
		s   = addr->m_name2.m_str;
		len = addr->m_name2.m_strlen;
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy( s , len );
	pbuf->safePrintf("</nobr></td>");
	
	
	// street
	len = 0;
	if ( addr && addr->m_street.m_str ) { 
		s   = addr->m_street.m_str;
		len = addr->m_street.m_strlen;
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy( s , len );
	pbuf->safePrintf("</nobr></td>");
	
	// city
	len = 0;
	if ( addr && addr->m_city.m_str ) {
		s   = addr->m_city.m_str;
		len = addr->m_city.m_strlen;
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy( s , len );
	pbuf->safePrintf("</nobr></td>");
	
	// adm1
	len = 0;
	if ( addr && addr->m_adm1.m_str ) {
		s   = addr->m_adm1.m_str;
		len = addr->m_adm1.m_strlen;
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy( s , len );
	pbuf->safePrintf("</nobr></td>");
	
	// ctry
	len = 0;
	if ( addr && addr->m_ctry.m_str ) {
		s   = addr->m_adm1.m_str;
		len = addr->m_adm1.m_strlen;
	}
	pbuf->safePrintf("<td><nobr>");
	pbuf->safeMemcpy( s , len );
	pbuf->safePrintf("</nobr></td>");
	*/
	
	pbuf->safePrintf("<td>");
	/*
	// print out the event description
	char broke = 0;
	for ( long j = a ; j < b ; j++ ) {
	// get section
	Section *sj = sp[j];
	// is word in a menu section? skip that junk
	if ( sj->m_flags & badFlags ) continue;
	// convert some tags to br
	if ( ! broke && isBreakingTagId(tids[j]) ) {
	broke = 1;
	pbuf->safeMemcpy(" * ",3);
	continue;
	}
	// skip if tag
	if ( tids[j] ) continue;
	// print the word token
	pbuf->safeMemcpy ( ww->m_words[j] ,ww->m_wordLens[j] );
	// break required again after an alnum
	if ( wids[j] ) broke = 0;
	}
	*/
	
	//
	//
	// print out headers (auxillary event descriptions)
	//
	//
	
	
	printEventDescription ( pbuf, ev , false );

	
	// close it up
	pbuf->safePrintf("</td>");


	//pbuf->safePrintf("<td>%s</td>",ev->m_tagsPtr);
	
	//
	// final row close
	//
	pbuf->safePrintf("</tr>\n");

	return true;
}

bool Events::printEventForCheckbox ( Event *ev , 
				     SafeBuf *pbuf , 
				     long long uh64 ,
				     long i ,
				     char *boxPrefix ) {

	Address *addr = ev->m_address;

	if ( ! addr ) return true;

	// . get eventdatea
	// . make it backwards compatible since we added at and on as
	//   date connecting words
	/*
	long long h_at = hash64n("at");
	long long h_on = hash64n("on");
	long       da   = ev->m_date->m_a;
	long       db   = ev->m_date->m_b;
	if ( db <= da ) db = da + 20;
	long long *wids = m_words->getWordIds  ();
	if ( da + 20 > db ) db = da+20;
	bool gotit = false;
	for ( long i = da ; i < db ; i++ ) {
		if ( wids[i] == h_at ||
		     wids[i] == h_on ) {
			gotit = true;
			da = i;
			break;
			continue;
		}
		if ( ! wids[i] ) continue;
		if ( gotit ) {
			da = i;
			break;
		}
	}
	// set this
	ev->m_eventCmpId = da;
	*/

	pbuf->safePrintf(
			 "<!--ignore-->" // ignore for Test.cpp diff
			 "<span class=validated>"
			 "<input type=checkbox "
			 "id=\"%s%li\" "
			 "onclick=\"senddiv(this,'%lli');\" "
			 "unchecked>"
			 "<div class=validated "
			 "style=\"display:none\" eventdatea=\"%li\">",
			 boxPrefix,i,
			 // must be unsigned
			 uh64,
			 ev->m_date->m_a ); // da

	// mod this to be in local time again so we do not have to re-check
	// hundreds of checkboxes
	long mm = ev->m_maxStartTime;
	// undo effects of mktime_utc()
	mm += timezone;
	// . undo effects of Events.cpp adding the timezoneoffset
	// . actually that code is now in Dates.cpp::getIntervals2() so undo
	//   what that does... (mdw apr 26, 2011)
	char useDST;
	char tz = ev->m_address->getTimeZone(&useDST);
	mm += 3600 * tz;
	// now we also deal with DST too!
	if ( useDST && getIsDST(mm,tz) ) mm += 3600;
	// max start time
	pbuf->safePrintf("%li",mm);//ev->m_maxStartTime );
	pbuf->pushChar(';');
	
	// title start and end
	pbuf->safePrintf("%li",ev->m_titleStart );
	pbuf->pushChar(';');
	pbuf->safePrintf("%li",ev->m_titleEnd );
	pbuf->pushChar(';');

	// now print the event flags using commas as delimeters
	printEventFlags ( pbuf , ev->m_flags );
	pbuf->pushChar(';');	
	
	
	// put the actual title now too so we can see it if it
	// does not match 
	if ( ev->m_titleStart >= 0 ) {
		// print it out
		long a = ev->m_titleStart;
		long b = ev->m_titleEnd;
		for ( long i = a ; i < b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if tag id
			if ( m_tids[i] ) continue;
			// encode utf8 chars as ascii for our txt file
			pbuf->javascriptEncode(m_wptrs[i],m_wlens[i]);
		}
	}
	pbuf->pushChar(';');
	
	
	char *a ;
	long  alen;
	
	if ( addr->m_flags & AF_VERIFIED_PLACE_NAME_1 ) {
		a    = addr->m_name1->m_str;
		alen = addr->m_name1->m_strlen;
		if ( a )  pbuf->javascriptEncode(a,alen);
	}
	pbuf->pushChar(';');
	
	if ( addr->m_flags & AF_VERIFIED_PLACE_NAME_2 ) {
		a    = addr->m_name2->m_str;
		alen = addr->m_name2->m_strlen;
		if ( a )  pbuf->javascriptEncode(a,alen);
	}
	pbuf->pushChar(';');
	
	a    = addr->m_street->m_str;
	alen = addr->m_street->m_strlen;
	if ( a )  pbuf->javascriptEncode(a,alen);
	pbuf->pushChar(';');

	if ( addr->m_city ) {
		a    = addr->m_city->m_str;
		alen = addr->m_city->m_strlen;
		if ( a )  pbuf->javascriptEncode(a,alen);
	}
	pbuf->pushChar(';');

	if ( addr->m_adm1 ) {
		a    = addr->m_adm1->m_adm1;//str;
		alen = 2;//addr->m_adm1->m_strlen;
		if ( a )  pbuf->javascriptEncode(a,alen);
	}
	pbuf->pushChar(';');

	if ( addr->m_zip ) {
		a    = addr->m_zip->m_str;
		alen = addr->m_zip->m_strlen;
		if ( a )  pbuf->javascriptEncode(a,alen);
	}
	pbuf->pushChar(';');
	
	//a    = addr->m_ctry.m_str;
	//alen = addr->m_ctry.m_strlen;
	//if ( a )  pbuf->javascriptEncode(a,alen);
	pbuf->pushChar(';');
	
	// now we include the event description!
	printEventDescription(pbuf,ev,true);
	
	pbuf->safePrintf ("\n</div>");
	pbuf->safePrintf ("</span>");
	return true;
}

void Events::printEventDescription ( SafeBuf *pbuf, Event *ev, bool hidden ) {

	// shortcut
	//Words *ww = m_words;

	// . get the smallest section containing the event
	Section *sn = ev->m_section;
	
	// . then description, the entire section i guess
	// . get range
	long a = sn->m_a;
	long b = sn->m_b;
	// if crazy show that. an open section.
	if ( b < 0 || b < a ) {
		pbuf->safePrintf("**open section**</td></tr>\n");
		return;
	}

	// shortcuts
	//Section    **sp  = m_sections->m_sectionPtrs;
	//nodeid_t   *tids = m_words->getTagIds();
	//long long *wids  = m_words->getWordIds  ();

	/*
	  // this logic is now in the min/maxEventId algo
	// there are bad section flags
	sec_t badFlags = SEC_MARQUEE|SEC_SELECT|SEC_SCRIPT|SEC_STYLE|
		SEC_NOSCRIPT|SEC_HIDDEN;
	// duplicated sections are now bad too
	//badFlags |= SEC_DUP;
	// no, each page could be mirrored and thereby have SEC_DUP set,
	// like a printer friendly page or whatever, so we have to use
	// the new SEC_MENU flag
	badFlags |= SEC_MENU;
	badFlags |= SEC_MENU_HEADER;
	badFlags |= SEC_INPUT_HEADER;
	badFlags |= SEC_INPUT_FOOTER;
	*/
	
	// nobr it
	if ( ! hidden ) pbuf->safePrintf("<nobr>");
	

	//long nw = m_words->getNumWords();
	// accumulate a description hash
	bool lastWasBrackets = true;

	// we now scan the words and hash those with an eventId range
	// in place of the date field in the datedb key
	//for ( long i = 0 ; i < nw ; i++ ) {
	for ( Section *si = m_sections->m_rootSection ; si ; si = si->m_next){
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not in description right now, no need to score it!
		if ( si->m_minEventId <= 0 ) continue;
		// breathe
		//QUICKPOLL(m_niceness);
		// skip if not alnum
		//if ( ! wids[i] ) continue;
		// get hsi section
		//Section *si = sp[i];
		// must be hashable
		//if ( si->m_minEventId <= 0 ) continue;
		// must be a sentence now
		if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// set this
		//long i = si->m_firstWordPos;
		long i = si->m_senta;
		// end of sentence 
		//long j = si->m_lastWordPos + 1;
		long j = si->m_sentb;
		/*
		// now get all words following in this section
		long j = i + 1;
		// stop when we hit a different section!
		for ( ; j < nw ; j++ ) {
			// get jth word section
			Section *sj = sp[j];
			// stop if different
			if ( sj != si ) break;
		}
		*/
		// save i
		long start = i;
		// advance i to this for the for loop
		i = j - 1;
		// if we did this section already because it was split,
		// skip it then (SEC_SPLIT_SENT)
		if ( si->m_senta < si->m_a ) continue;
		// skip if overflowed. right now we only got 8 bits
		if ( si->m_maxEventId > 255 ) continue;
		// skip if not for us
		//if ( ev->m_eventId < si->m_minEventId ) continue;
		//if ( ev->m_eventId > si->m_maxEventId ) continue;

		uint64_t h = (uint32_t)si;
		h <<= 32;
		// mix it up!
		uint32_t evkey = hash32h((uint32_t)ev,12345);
		h |= (uint32_t)evkey;
		// get title score ptr
		float *tsp = (float *)m_titleScoreTable.getValue(&h);

		// get the event specific flags
		esflags_t esflags = getEventSentFlags(ev,si,m_evsft);

		//if ( esflags & EVSENT_SUBEVENTBROTHER )
		//	log("hey");

		// do not print if descScore is 0 either and not title.
		// that means it had SEC_MENU or SEC_MENU_HEADER set. it
		// was still considered as a title candidate, but unless it
		// was selected as the title we completely ignore it.
		// this fixes southgatehouse.com which has titles that are
		// a few consecutive links of the bands playing that night.
		// they are still marked as SEC_MENU but they can be the title
		// now, and since they do not get TF_PAGE_REPEAT set because
		// they are unique, they make it as the title.
		if ( (esflags & EVSENT_DONOTPRINT) &&
		     // do not skip if title ever
		     ev->m_titleSection != si ) 
			continue;

		// if Event::m_flags have EV_ADCH32 set then
		if ( ev->m_flags & EV_BAD_EVENT ) continue;

		// must be us
		if ( ! si->hasEventId ( ev->m_eventId ) &&
		     // unless its a subevent brother in which case let it thru
		     ! (esflags & EVSENT_SUBEVENTBROTHER) )
			continue;

		// get our event id as a byte offset and bit mask
		//unsigned char byteOff = ev->m_eventId / 8;
		//unsigned char bitMask = 1 << (ev->m_eventId % 8);
		// make sure our bit is set
		//if ( ! ( si->m_evIdBits[byteOff] & bitMask ) ) continue;

		// print telescope signal
		if ( ! lastWasBrackets ) {
			pbuf->safePrintf(" [[]] ");
			lastWasBrackets = true;
		}
		// print it out
		a = start;//ev->m_desc2a;
		b = j;//sn->m_desc2b;
		// this is 0 for subeventbrothers
		float tscore = 0.0;
		if ( tsp ) tscore = *tsp;
		//bool printedSomething = false;
		// nobr it
		//pbuf->safePrintf("<nobr>");
		// print out the event description
		bool printed = printEventSentence ( si ,
						    ev,
						    si->m_sentFlags,
						    esflags,
						    a ,
						    b ,
						    hidden ,
						    tscore,
						    pbuf );
		if ( printed )
			lastWasBrackets = false;
		// print out any associated subsentence title candidate
		for ( long i = 0 ; i < m_numSubSents ; i++ ) {
			SubSent *ss = &m_subSents[i];//s_subsents[i];
			if ( ss->m_senta < si->m_senta ) continue;
			if ( ss->m_senta >= si->m_sentb ) break;
			// print this to separate
			char *sub = " ((subsent)) ";
			long  sublen = strlen(sub);
			pbuf->safeStrcpy( sub );
			// get the event specific flags
			esflags_t esflags =getEventSubSentFlags(ev,ss,m_evsft);
			bool printed = printEventSentence ( si ,
							    ev ,
							    ss->m_subSentFlags,
							    esflags,
							    ss->m_senta,
							    ss->m_sentb,
							    hidden,
							    ss->m_titleScore,
							    pbuf );
			if ( printed )
				lastWasBrackets = false;
			// back up over last
			else
				pbuf->m_length -= sublen;
		}
	}
	// nobr it
	if ( ! hidden ) pbuf->safePrintf("</nobr>");
}

bool Events::printEventSentence ( Section *si ,
				  Event *ev ,
				  sentflags_t sflags ,
				  esflags_t esflags ,
				  long a ,
				  long b ,
				  bool hidden ,
				  float tscore ,
				  SafeBuf *pbuf ) {

	bool printedSomething = false;
	char broke = 0;
	//unsigned long descHash32 = 0;
	nodeid_t   *tids = m_words->getTagIds();
	long long *wids  = m_words->getWordIds  ();
	
	// is it indexable?
	bool indexIt = (esflags & EVSENT_IS_INDEXABLE);

	for ( long k = a ; k < b ; k++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// convert some tags to br
		if ( ! broke && isBreakingTagId(tids[k]) ) {
			broke = 1;
			pbuf->safeMemcpy(" * ",3);
			continue;
		}
		// skip if tag
		if ( tids[k] ) continue;
		// shortcuts
		char *ws = m_words->m_words[k];
		long  wslen = m_words->m_wordLens[k];
		// . print the word token
		// . encode utf8 chars as ascii for our txt file
		if ( hidden )
			pbuf->javascriptEncode(ws,wslen);
		else {
			if ( ! printedSomething && indexIt )
				pbuf->safePrintf("<b>");
			pbuf->safeMemcpy ( ws , wslen );
		}
		// note it
		printedSomething = true;
		//lastWasBrackets = false;
		// accumulate the hash
		//descHash32 ^= (unsigned long)wids[k];
		// break required again after an alnum
		if ( wids[k] ) broke = 0;
	}
	if ( ! printedSomething ) return false;
	// just skip all this if hidden i guess
	if ( hidden ) return true;
	// close any bold tag
	if ( indexIt ) 
		pbuf->safePrintf("</b>");
	// then the descscore/titlescore
	pbuf->safePrintf("<font color=red>");

	// show the voting factor
	float factor = si->getSectiondbVoteFactor();

	// for debugging
	if ( tscore != 100.0 && factor != 1.0 ) 
		pbuf->safePrintf(" ts=%.04f"
				 " [sec_votes=%.02f(dup=%li,notdup=%li)]",
				 tscore,factor,
				 si->m_votesForDup,
				 si->m_votesForNotDup);
	else if ( tscore != 100.0 ) 
		pbuf->safePrintf(" ts=%.04f",tscore);

	// mix it up!
	uint32_t evkey = hash32h((uint32_t)ev,12345);
	uint64_t key = (((uint64_t)si)<<32) | (uint32_t)evkey;
	// this can be NULL for subeventbrothers
	float *dscorep = (float *)m_descScoreTable.getValue ( &key );
	float dscore = 0.0;
	if ( dscorep ) dscore = *dscorep;

	if ( dscore == 0.0 )
		pbuf->safePrintf(" ds=0");
	else
		pbuf->safePrintf(" ds=%.04f",dscore);

#ifdef _USETURKS_
	/*
	// grab turkbits for section/event
	turkbits_t *tbp = (turkbits_t *)m_tbt->getValue ( &key );
	turkbits_t tb = 0;
	if ( tbp ) tb = *tbp;
	for ( long i = 0 ; i < (long)sizeof(turkbits_t)*8 ; i++ ) {
		uint64_t mask = ((turkbits_t)1) << (turkbits_t)i;
		//if ( ! ((si->m_turkBits) & mask ) ) continue;
		if ( ! (tb & mask) ) continue;
		pbuf->safePrintf(" %s",getTurkBitLabel(mask));
	}
	*/
#endif

	long ess = sizeof(esflags_t) * 8;
	for ( long i = 0 ; i < ess ; i++ ) {
		uint64_t mask = ((uint64_t)1) << (uint64_t)i;
		if ( ! (esflags & mask ) ) continue;
		pbuf->safePrintf(" %s",getEventSentBitLabel(mask));
	}

	for ( long i = 0 ; i < 64 ; i++ ) {
		uint64_t mask = ((uint64_t)1) << (uint64_t)i;
		if ( ! (sflags & mask ) ) continue;
		pbuf->safePrintf(" %s",getSentBitLabel(mask));
	}

	// print certain section bits
	//if ( si->m_flags & SEC_MENU_SENTENCE )
	//	pbuf->safePrintf(" menusentence");
	// then the descscore/titlescore
	pbuf->safePrintf("</font>");
	return true;
}


// . index each event into the datedb hash table
// . "wts" maps a word/phrase id to a stringPtr/stringLen and is used for
//   PageParser.cpp for printing out what we hash in all the tables
// . returns false and sets g_errno on error
// . TODO: FUTURE eventdb term key:
//   termid48bits|tier5bits|docid38bits|langId4Bits|adult1Bit|
//   ev1Id8bits|ev2Id8bits|compressBit1|wordPos12bits|compressBit2|delBit
bool Events::hash ( //long        baseScore ,
		    //long        version   ,
		    HashTableX *dt        ,
		    SafeBuf    *pbuf      ,
		    HashTableX *wts       ,
		    SafeBuf    *wbuf      ,
		    long        numHashableEvents ) {

	long version = TITLEREC_CURRENT_VERSION;

	// shortcuts
	long         nw = m_words->getNumWords();
	long long *wids = m_words->getWordIds  ();
	Section    **sp = m_sections->m_sectionPtrs;

	// sanity check
	if ( dt->m_ks != sizeof(key96_t) ) { char *xx=NULL;*xx=0; }

	// must have had some events!
	//if ( m_numValidEvents <= 0 ) return true;
	//if ( m_revisedValid <= 0 ) return true;

	// map the event id to the event ptr for use below
	Event *map[256];
	memset ( map , 0 , 256 * sizeof(Event *) );
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get ith event
		Event *ev = &m_events[i];
		// get id
		long eid = ev->m_eventId;
		// skip if not valid
		if ( eid <= 0 ) continue;
		// ignore if over 255
		if ( eid > 255 ) continue;
		// map it
		map[eid] = ev;
	}

	// hash words in [i,j)
	HashInfo hi;
	// make a fake date, 
	long fakeDate = 0;
	// cast it
	uint8_t *fp = (uint8_t *)&fakeDate;

	// we now scan the words and hash those with an eventId range
	// in place of the date field in the datedb key
	for ( long i = 0 ; i < nw ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not alnum
		if ( ! wids[i] ) continue;
		// get hsi section
		Section *si = sp[i];

		// need sentence
		si = si->m_sentenceSection;
		// sometimes <option> menu. not a sentence...
		if ( ! si ) continue;

		// . skip if not belonging to an event id range
		// . only sections we should hash have a valid range
		// . we set this above 
		if ( si->m_minEventId <= 0 ) continue;

		//float descScore = si->m_descScore;

		// . if zero change to title score
		// . sometimes like for southgatehouse.com we select
		//   something that has SEC_MENU set as part of the title.
		//   otherwise we should ignore such thing here too!!!
		//   PROBLEM: might be best title for another event id...?
		//   that's not a problem for indexing i don't think...
		//if ( descScore <= 0.0 && (si->m_sentFlags &SENT_BEST_TITLE))
		//	descScore = si->m_titleScore;

		// . if desc score is 0 do not hash it, unless title!
		// . should fix 'opera' query to remove that one result
		//if ( descScore <= 0.0 ) continue;

		// compute score. descScore ranges from 0 to 100.0
		//long score = (long)((((float)baseScore*descScore)/100.0)+.5);

		// don't use descScore because it is punished for being
		// in multiple events
		//long score = (long)baseScore;

		// hashWords() will core if this is zero
		//if ( score <= 0 ) score = 1;

		// now get all words following in this section
		long j = i + 1;
		// stop when we hit a different section!
		for ( ; j < nw ; j++ ) {
			// get jth word section
			Section *sj = sp[j];
			// stop if different
			// AND not a subset!
			if ( sj != si && sj->m_a >= si->m_b ) break;
		}
		// save i
		long start = i;
		// advance i to this for the for loop
		i = j - 1;

		// skip if overflowed. right now we only got 8 bits
		if ( si->m_maxEventId > 255 ) continue;

		// now we no longer have the nicety of having just a 
		// contiguous sequence of event ids, but usually we do...
		// so hash each eventid range we have, usually this is just
		// one event id range, but sometimes not!
		long bstart = -1;
		long bend   = -1;
		for ( long k = si->m_minEventId; k <= si->m_maxEventId+1;k++){
			// breathe
			QUICKPOLL(m_niceness);
			// get the bit mask
			if ( si->hasEventId (k) ) {
				// get event with eventId "k"
				Event *ek = map[k];
				// but skip if invalidated
				if ( ek->m_flags & EV_DO_NOT_INDEX ) continue;
				esflags_t esflags;
				esflags = getEventSentFlags (ek,si,m_evsft);
				// skip if not indexable
				if (!(esflags & EVSENT_IS_INDEXABLE ))continue;
				// sanity
				if ( ek->m_indexedEventId <= 0 ) {
					char*xx=NULL;*xx=0;}
				// no! sometimes dscore is 0 like when it
				// has EVSENT_HASEVENTDATE flag set for some
				// reason the dscore is 0!!!
				// sanity check -- get dscore -- sanity check
				//uint64_tkey=(((uint64_t)si)<<32)|(uint32_t)ek
				//float dscore = *(float *)m_descScoreTable.
				//	getValue ( &key );
				//if ( dscore == 0.0 ) {char *xx=NULL;*xx=0;}
				// start recording
				//if ( bstart == -1 ) bstart = k;
				if ( bstart==-1 ) bstart =ek->m_indexedEventId;
				// always update this
				//bend = k ;
				bend = ek->m_indexedEventId;
				// keep going
				continue;
			}
			// . index it otherwise
			// . must have something
			if ( bstart == -1 ) break;
			// sanity
			if ( bend > numHashableEvents ) { char *xx=NULL;*xx=0;}
			// sanity!
			if ( bstart <= 0 ) { char *xx=NULL;*xx=0; }
			if ( bend   <= 0 ) { char *xx=NULL;*xx=0; }
			// store eventId range in there
			fp[1] = bstart;//si->m_minEventId;
			fp[0] = bend; // si->m_maxEventId;
			// . try to keep some form of tiering here
			// . so if we have a high scoring title term
			//   it will at least be on top of the list
			// . no, we can't do tiering anyway because of
			//   searching by lon/lat/times, etc....
			//fp[3] = 255 - score8;
			// make the date actually an 8-bit eventId
			hi.m_date      = fakeDate;
			hi.m_tt        = dt;
			//hi.m_baseScore = score;//baseScore;
			hi.m_useSynonyms = true;
			hi.m_prefix      = NULL;
			/*
			// . hash the words/phrases in it with the given date!
			// . TODO: verify with trumba rss feeds
			if ( ! hashWords ( start      ,
					   j          , // end
					   &hi        ,
					   m_words    ,
					   m_phrases  ,
					   m_synonyms ,
					   m_sections,
					   NULL , // counttable
					   NULL , // fragvec
					   NULL , // wordspamvec
					   //m_weights  ,
					   pbuf       , // pbuf
					   wts        ,
					   wbuf       ,
					   m_niceness ) )
				return false;
			*/
			// reset start
			bstart = -1;
		}
	}


	// make the date actually an 8-bit eventId
	hi.m_tt          = dt;
	//hi.m_baseScore   = baseScore;
	hi.m_useSynonyms = true;
	hi.m_prefix      = "title";
	// pre-grow "dt" the interval table
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event 
		Event *ev = &m_events[i];
		// but skip if invalidated
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		// store eventId range in there
		fp[1] = ev->m_indexedEventId;
		fp[0] = ev->m_indexedEventId;
		// set it
		hi.m_date = fakeDate;
		/*
		// now hash as title for a title: search
		if ( ! hashWords ( ev->m_titleStart ,
				   ev->m_titleEnd   ,
				   &hi        ,
				   m_words    ,
				   m_phrases  ,
				   m_synonyms ,
				   m_sections,
				   NULL , // counttable
				   NULL , // fragvec
				   NULL , // wordspamvec
				   //m_weights  ,
				   pbuf       , // pbuf
				   wts        ,
				   wbuf       ,
				   m_niceness ) )
			return false;
		*/
	}

	long need = 0;
	// pre-grow "dt" the interval table
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event 
		Event *ev = &m_events[i];
		// but skip if invalidated
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		// must have an address
		if ( ! ev->m_address ) continue;

		// how many intervals this has
		need += ev->m_ni;
		// convert to a sortable score
		// it goes from -180 to 170
		double latitude  ;//= ev->m_address->m_latitude;
		double longitude ;//= ev->m_address->m_longitude;
		ev->m_address->getLatLon(&latitude,&longitude);
		// skip if no valid gps coordinates
		if ( latitude  == NO_LATITUDE  ) continue;
		if ( longitude == NO_LONGITUDE ) continue;
		// normalize to get in range [0,360.0]
		// we already do this now in Address.cpp
		longitude += 180.0;
		latitude  += 180.0;
		// sanity check
		if ( longitude <   0.0 ) continue;
		if ( latitude  <   0.0 ) continue;
		if ( longitude > 360.0 ) continue;
		if ( latitude  > 360.0 ) continue;
		need += 2;
	}
	// pre-grow
	if ( ! dt->setTableSize ( need * 2 , NULL , 0 ) ) return false;

	// allow dups for this loop
	dt->m_allowDups = true;

	// loop over all events and hash their time intervals and their
	// address into datedb
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event 
		Event *ev = &m_events[i];
		// but skip if invalidated
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		// must have an address
		if ( ! ev->m_address ) continue;

		// then hash gbcity:, gbcountry:, gbadm1:, gbadm2:, gbzip:,
		// gbstreet: and gbname: and everything under gbaddress:
		Address *addr = ev->m_address;
		// . for now skip lat/lon only addresses
		// . no, now we find the nearest city and hash that
		//   and the country, etc.
		bool skip = ( addr->m_flags3 & AF2_LATLON );

		// . base score is the event id, i take it
		// . but reverse map it since we compute to 8 bits later
		// . make score the event id
		long score = score8to32 ( ev->m_indexedEventId );

		// the date is the event id range when hashing gbaddress: stuff
		uint32_t date = ev->m_indexedEventId;
		// its a range, two bytes
		date <<= 8; date |= ev->m_indexedEventId;

		// hash it
		if ( //! skip &&
		     ! addr->hash ( 1          , // score      ,
				    dt         ,
				    date       ,
				    m_words    ,
				    m_phrases  ,
				    pbuf       ,
				    wts        ,
				    wbuf       ,
				    version    ,
				    m_niceness ))
			return false;

		hi.m_date   = date;
		// shortcut
		Place *vn = ev->m_bestVenueName;

		/*
		// hash the place names, no longer part of the Address::hash()
		hi.m_prefix = "gbaddress";
		if ( vn && ! hashString ( vn->m_str    ,
					  vn->m_strlen ,
					  &hi          ,
					  NULL         , // count table
					  pbuf         ,
					  wts          ,
					  wbuf         ,
					  version      ,
					  0            , // siteNumInlinks
					  m_niceness   ) )
			return false;
		*/

		char *venue = NULL;
		long  vlen  = 0;

		if ( vn && (addr->m_flags & AF_VERIFIED_PLACE_NAME_1) ) {
			venue = vn->m_str;
			vlen  = vn->m_strlen;
		}
		// sometimes is a "none" confirmed, so vn is NULL
		if ( ev->m_confirmedVenue && vn ) {
			venue = vn->m_str;
			vlen  = vn->m_strlen;
		}

		// . if it ends in and indicator that works too!
		// . go with it even if not confiremd but ends in indicator
		// . we display this in the search results as the venue name
		//   so let's be consistent and hash them as well
		if ( vn ) {
			venue = vn->m_str;
			vlen  = vn->m_strlen;
		}

		// . hash without prefix too!
		// . so they can search for "kimo theater" in case it is
		//   not included in the event description, but maybe
		//   referenced from the contact info page using the placedb
		//   record
		hi.m_prefix = NULL;
		if ( vn && ! hashString ( venue ,
					  vlen  ,
					  &hi          ,
					  NULL         , // count table
					  pbuf         ,
					  wts          ,
					  wbuf         ,
					  version      ,
					  0            , // siteNumInlinks
					  m_niceness   ) )
			return false;

		// hash gbwhere: terms for venue name
		hi.m_prefix = "gbwhere";
		if ( vn && ! hashString ( venue ,
					  vlen  ,
					  &hi          ,
					  NULL         , // count table
					  pbuf         ,
					  wts          ,
					  wbuf         ,
					  version      ,
					  0            , // siteNumInlinks
					  m_niceness   ) )
			return false;
		// venue street
		hi.m_prefix = NULL;
		if ( ! skip &&
		     ! hashString ( ev->m_address->m_street->m_str,
				    ev->m_address->m_street->m_strlen,
				    &hi          ,
				    NULL         , // count table
				    pbuf         ,
				    wts          ,
				    wbuf         ,
				    version      ,
				    0            , // siteNumInlinks
				    m_niceness   ) )
			return false;
		// venue street
		hi.m_prefix = "gbwhere";
		if ( ! skip &&
		     ! hashString ( ev->m_address->m_street->m_str,
				    ev->m_address->m_street->m_strlen,
				    &hi          ,
				    NULL         , // count table
				    pbuf         ,
				    wts          ,
				    wbuf         ,
				    version      ,
				    0            , // siteNumInlinks
				    m_niceness   ) )
			return false;
		// venue city
		char *cityStr = NULL;
		long  slen;
		if ( ev->m_address->m_city ) {
			cityStr = ev->m_address->m_city->m_str;
			slen    = ev->m_address->m_city->m_strlen;
		}
		else if ( ev->m_address->m_zip ) {
			cityStr = ev->m_address->m_zip->m_cityStr;
			slen    = gbstrlen(cityStr);
		}
		// latlon based addresses do not have anything else
		if ( ! cityStr && 
		     ! (ev->m_address->m_flags3 & AF2_LATLON) ) { 
			char *xx=NULL;*xx=0; }
		hi.m_prefix = NULL;
		if ( cityStr &&
		     ! hashString ( cityStr      ,
					  slen         ,
					  &hi          ,
					  NULL         , // count table
					  pbuf         ,
					  wts          ,
					  wbuf         ,
					  version      ,
					  0            , // siteNumInlinks
					  m_niceness   ) )
			return false;
		// venue city
		hi.m_prefix = "gbwhere";
		if ( cityStr &&
		     ! hashString ( cityStr      ,
					  slen         ,
					  &hi          ,
					  NULL         , // count table
					  pbuf         ,
					  wts          ,
					  wbuf         ,
					  version      ,
					  0            , // siteNumInlinks
					  m_niceness   ) )
			return false;
		// venue state
		char *adm1Str = NULL;
		if      ( ev->m_address->m_adm1 ) {
			adm1Str = ev->m_address->m_adm1->m_str;
			slen    = ev->m_address->m_adm1->m_strlen;
		}
		else if ( ev->m_address->m_zip ) {
			adm1Str = ev->m_address->m_zip->m_adm1;
			slen    = gbstrlen(adm1Str);
		}
		if ( ! adm1Str &&
		     ! (ev->m_address->m_flags3 & AF2_LATLON) ) { 
			char *xx=NULL;*xx=0; }
		hi.m_prefix = NULL;
		if ( adm1Str &&
		     ! hashString ( adm1Str ,
					  slen    ,
					  &hi          ,
					  NULL         , // count table
					  pbuf         ,
					  wts          ,
					  wbuf         ,
					  version      ,
					  0            , // siteNumInlinks
					  m_niceness   ) )
			return false;
		hi.m_prefix = "gbwhere";
		if ( adm1Str &&
		     ! hashString ( adm1Str ,
					  slen    ,
					  &hi          ,
					  NULL         , // count table
					  pbuf         ,
					  wts          ,
					  wbuf         ,
					  version      ,
					  0            , // siteNumInlinks
					  m_niceness   ) )
			return false;

		// custom synonyms for state abbreviations
		StateDesc *sd = NULL;
		if ( slen == 2 && adm1Str ) sd = getStateDesc ( adm1Str );
		// hash the first synonym
		hi.m_prefix = NULL;
		if ( sd && sd->m_name1 &&
		     ! hashString ( sd->m_name1 ,
				    gbstrlen(sd->m_name1),
				    &hi          ,
				    NULL         , // count table
				    pbuf         ,
				    wts          ,
				    wbuf         ,
				    version      ,
				    0            , // siteNumInlinks
				    m_niceness   ) )
			return false;
		hi.m_prefix = "gbwhere";
		if ( sd && sd->m_name1 &&
		     ! hashString ( sd->m_name1 ,
				    gbstrlen(sd->m_name1),
				    &hi          ,
				    NULL         , // count table
				    pbuf         ,
				    wts          ,
				    wbuf         ,
				    version      ,
				    0            , // siteNumInlinks
				    m_niceness   ) )
			return false;

		// hash the 2nd synonym
		hi.m_prefix = NULL;
		if ( sd && sd->m_name2 &&
		     ! hashString ( sd->m_name2 ,
				    gbstrlen(sd->m_name2),
				    &hi          ,
				    NULL         , // count table
				    pbuf         ,
				    wts          ,
				    wbuf         ,
				    version      ,
				    0            , // siteNumInlinks
				    m_niceness   ) )
			return false;
		hi.m_prefix = "gbwhere";
		if ( sd && sd->m_name2 &&
		     ! hashString ( sd->m_name2 ,
				    gbstrlen(sd->m_name2),
				    &hi          ,
				    NULL         , // count table
				    pbuf         ,
				    wts          ,
				    wbuf         ,
				    version      ,
				    0            , // siteNumInlinks
				    m_niceness   ) )
			return false;
			

		// take it off
		hi.m_prefix = NULL;

		// . THIS NOW USES TIMEDB! and addInterval() func below
		// . see Timedb.h for key format
		//if ( ! hashIntervals ( ev , tmt ) ) //, wts , wbuf ) )
		//	return false;

		key96_t key;

		uint64_t prefix = hash64n("gbstorehours");
		// hash gbstorehours0|1
		char *str = "0";
		if ( ev->m_flags & EV_STORE_HOURS ) str = "1";
		// hash that
		uint64_t val = hash64n ( str );
		// combine
		uint64_t termId = hash64h(val,prefix);
		//if ( ! hashSingleTerm ( str , gbstrlen(str), &hi );
		key.n0 = termId;//hash64n(str);
		// fake date is event id range
		key.n1 = date;
		// fake this
		long simpleScore = 1;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;


		evflags_t mask = 0;
		mask |= EV_HASTITLEWORDS;
		mask |= EV_HASTITLEFIELD;
		mask |= EV_HASTITLESUBSENT;
		mask |= EV_HASTITLEBYVOTES;
		mask |= EV_HASTITLEWITHCOLON;
		prefix = hash64n("gbhastitleindicator");
		str = "0";
		if ( ev->m_flags & mask ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// do each one individually for debugging searches
		prefix = hash64n("gbhastitlewords");
		str = "0";
		if ( ev->m_flags & EV_HASTITLEWORDS ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;
		// do each one individually for debugging searches
		prefix = hash64n("gbhastitlefield");
		str = "0";
		if ( ev->m_flags & EV_HASTITLEFIELD ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;
		// do each one individually for debugging searches
		prefix = hash64n("gbhastitlesubsent");
		str = "0";
		if ( ev->m_flags & EV_HASTITLESUBSENT ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;
		// do each one individually for debugging searches
		prefix = hash64n("gbhastitlebyvotes");
		str = "0";
		if ( ev->m_flags & EV_HASTITLEBYVOTES ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;
		// do each one individually for debugging searches
		prefix = hash64n("gbhastitlewithcolon");
		str = "0";
		if ( ev->m_flags & EV_HASTITLEWITHCOLON ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gblongduration");
		str = "0";
		if ( ev->m_flags & EV_LONGDURATION ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbprivate");
		str = "0";
		if ( ev->m_flags & EV_PRIVATE ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbfacebook");
		str = "0";
		if ( ev->m_flags & EV_FACEBOOK ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbstubhub");
		str = "0";
		if ( ev->m_flags & EV_STUBHUB ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbeventbrite");
		str = "0";
		if ( ev->m_flags & EV_EVENTBRITE ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbincrazytable");
		str = "0";
		if ( ev->m_flags & EV_INCRAZYTABLE ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// some date hashes
		prefix = hash64n("gbhastightdate");
		str = "0";
		if ( ev->m_date->m_flags & DF_TIGHT ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbinjected");
		str = "0";
		if ( m_xd &&
		     m_xd->m_oldsrValid &&
		     m_xd->m_oldsr.m_isInjecting )
			str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;


		// some date hashes. these are not tight...
		prefix = hash64n("gbincalendar");
		str = "0";
		if ((ev->m_date->m_flags & DF_TABLEDATEHEADERCOL) ||
		    (ev->m_date->m_flags & DF_TABLEDATEHEADERROW) ) 
			str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// some date hashes
		prefix = hash64n("gbhascomplexdate");
		str = "0";
		if ( ev->m_date->m_type == DT_TELESCOPE ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		bool isDaily   = false;
		bool isWeekly  = false;
		bool isMonthly = false;

		long ndow = getNumBitsOn8 ( ev->m_date->m_dowBits );

		// . DAILY EVENTS
		// . 3+ days a week is considered daily...
		// . need a total of 3 months worth too!
		prefix = hash64n("gbdaily");
		str = "0";
		if ( ndow >= 3 && ev->m_ni >= 30*3*3 ) {
			isDaily = true;
			str = "1";
		}
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// . WEEKLY EVENTS
		// . weekly date for zak's categories
		// . 16 weeks worth of events, consider weekly
		// . at least 1 dow involved
		prefix = hash64n("gbweekly");
		str = "0";
		if ( ndow > 0 && ev->m_ni / ndow >= 16 ) {
			isWeekly = true;
			str = "1";
		}
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// . MONTHLY EVENTS
		// . <= 2 days per month consider monthly
		// . for at least 3 months
		// . i think we go out two years when making the intervals
		//   and we also cap that too, at like a max of 104 or
		//   something... i *think*
		prefix = hash64n("gbmonthly");
		str = "0";
		if ( ndow <= 2 && ev->m_ni >= 8 && ev->m_ni <= 48 ) {
			isMonthly = true;
			str = "1";
		}
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// OTHER FREQUENCY EVENTS
		prefix = hash64n("gbinfrequently");
		str = "0";
		if ( ! isDaily && ! isWeekly && ! isMonthly ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		     

		// some date hashes
		prefix = hash64n("gbhasdaynum");
		str = "0";
		if ( ev->m_date->m_hasType & DT_DAYNUM ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbhasmonth");
		str = "0";
		if ( ev->m_date->m_hasType & DT_MONTH ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;
		
		prefix = hash64n("gbhasmonthdaynum");
		str = "0";
		if ( (ev->m_date->m_hasType & DT_MONTH) &&
		     (ev->m_date->m_hasType & DT_DAYNUM) )
			str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbhasrangedaynum");
		str = "0";
		if ( ev->m_date->m_hasType & DT_RANGE_DAYNUM ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbhasrangemonthday");
		str = "0";
		if ( ev->m_date->m_hasType & DT_RANGE_MONTHDAY ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbhasrangedow");
		str = "0";
		if ( ev->m_date->m_hasType & DT_RANGE_DOW ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbnumdows");
		char ndw[12];
		sprintf(ndw,"%li",getNumBitsOn8(ev->m_date->m_dowBits) );
		val = hash64n ( ndw );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;
		
		// hash gbeventhashxxxxxxx (in decimal)
		prefix = hash64n("gbeventhash");
		char ehbuf[256];
		sprintf(ehbuf,"%llu",ev->m_eventHash64);
		val = hash64n ( ehbuf );
		termId = hash64h(val,prefix);
		key.n0 = termId;//hash64n(ehbuf);
		key.n1 = date; // fake date is event id range
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// hash the address hash
		prefix = hash64n("gbaddresshash");
		sprintf(ehbuf,"%llu",ev->m_address->m_hash); // 64 bit
		val = hash64n ( ehbuf );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// hash gbeventaddressdatecontenthash32:xxxx
		prefix = hash64n("gbadch32");
		sprintf(ehbuf,"%lu",ev->m_adch32);
		val = hash64n ( ehbuf );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// hash gbeventaddresstitlecontenthash32:xxxx
		//prefix = hash32n("gbeventaddresstitlecontenthash32");
		//sprintf(ehbuf,"%lu",(long)ev->getAddressTitleContentHash32())
		//val = hash64n ( ehbuf );
		//termId = hash64h(val,prefix);
		//key.n0 = termId;
		//key.n1 = date;
		//if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		// hash gbeventaddressdatetaghash32:xxxx
		prefix = hash64n("gbadth32");
		sprintf(ehbuf,"%lu",ev->m_adth32);
		val = hash64n ( ehbuf );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key , &simpleScore ) ) return false;

		prefix = hash64n("gbhasvenue");
		str = "0";
		if ( vn ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key,&simpleScore ) ) return false;

		// ideally we want facebook events because they have pictures,
		// and we want them to have a venue and a street address,
		// not just pure lat/lon
		prefix = hash64n("gbislatlononly");
		str = "0";
		if ( ev->m_address->m_flags3 & AF2_LATLON ) str = "1";
		val = hash64n ( str );
		termId = hash64h(val,prefix);
		key.n0 = termId;
		key.n1 = date;
		if ( ! dt->addKey ( &key,&simpleScore ) ) return false;


		// hash gbeventaddresstitletaghash32:xxxx
		//prefix = hash64n("gbeventaddresstitletaghash32");
		//sprintf(ehbuf,"%lu",(long)ev->getAddressTitleTagHash32());
		//val = hash64n ( ehbuf );
		//termId = hash64h(val,prefix);
		//key.n0 = termId;
		//key.n1 = date;
		//if ( ! dt->addKey ( &key , &simpleScore ) ) return false;


		//
		// now hash the estimated latitude/longitude. that is, if
		// the event does not have a known lat/lon, just use the
		// centroid of the city. this way we can search this termlist
		// for events in your city, but of course, sort by distance
		// will not work. but most people will not sort by distance
		// in this case. they just want to see what is going on near
		// them.
		//
		double latitude  ;//= ev->m_address->m_latitude;
		double longitude ;//= ev->m_address->m_longitude;
		ev->m_address->getLatLon(&latitude,&longitude);

		// if that failed, get it from the city state
		if ( latitude == NO_LATITUDE )
			getCityLatLonFromAddress ( ev->m_address ,
						   &latitude     ,
						   &longitude    );
		// only hash if we got a valid city/state
		if ( latitude != NO_LATITUDE ) {
			// sanity check - not normalized
			if ( latitude  > 180.0 ) { char *xx=NULL;*xx=0; }
			if ( longitude > 180.0 ) { char *xx=NULL;*xx=0; }
			// normalize
			latitude  += 180.0;
			longitude += 180.0;
			// normalize the same way we do in Msg39::getList() now
			longitude *= 10000000.0;
			latitude  *= 10000000.0;
			// now convert to unsigned long score
			unsigned long lonDate = (unsigned long)longitude;
			unsigned long latDate = (unsigned long)latitude;
			// add the longitude
			//key.n0 = hash64n("gbxlongitudecity") ;
			key.n0 = hash64n("gbxlongitude2") ;
			// fake date
			key.n1 = lonDate;
			if ( ! dt->addKey ( &key , &score ) ) return false;
			// add the latitude
			//key.n0 = hash64n("gbxlatitudecity") ;
			key.n0 = hash64n("gbxlatitude2") ;
			// fake date
			key.n1 = latDate;
			if ( ! dt->addKey ( &key , &score ) ) return false;
		}

		//
		// now hash the exact lat/lon if known
		//

		// convert to a sortable score
		// it goes from -180 to 170
		// no, we normalized it in Address.cpp so its from 0 to 360 now
		//latitude  = ev->m_address->m_latitude;
		//longitude = ev->m_address->m_longitude;
		ev->m_address->getLatLon(&latitude,&longitude);

		// if we have no exact lat/lon for the event default to the
		// lat lon for the zip code... i am reluctant to default
		// to the city's lat/lon if we have no zip because that is
		// just too inaccurate usually. i am hoping zak comes through
		// with the geocoder he says he found.
		if ( latitude == NO_LATITUDE ) {
			float tmplatf;
			float tmplonf;
			getZipLatLonFromAddress ( ev->m_address ,
						  &tmplatf      , // float
						  &tmplonf      );// float
			if ( tmplatf == 999.0 ) continue;
			if ( tmplonf == 999.0 ) continue;
			latitude  = tmplatf;
			longitude = tmplonf;
		}

		// skip if no valid gps coordinates
		if ( longitude == 999.0 ) continue;
		if ( latitude  == 999.0 ) continue;
		// sanity check - must be unnormalized at this point
		if ( latitude  > 180.0 ) { char *xx=NULL;*xx=0; }
		if ( longitude > 180.0 ) { char *xx=NULL;*xx=0; }
		// normalize to get in range [0,360.0]
		// we already do this now in Address.cpp
		//longitude += 180.0;
		//latitude  += 180.0;
		latitude  += 180.0;
		longitude += 180.0;
		// sanity check
		if ( longitude <   0.0 ) continue;
		if ( latitude  <   0.0 ) continue;
		if ( longitude > 360.0 ) continue;
		if ( latitude  > 360.0 ) continue;
		// now normalize to scale into [0,0xffffffff]
		//longitude = ((double)0xffffffff) *  (longitude / 360.0);
		//latitude  = ((double)0xffffffff) *  (latitude  / 360.0);
		// normalize the same way we do in Msg39::getList() now
		longitude *= 10000000.0;
		latitude  *= 10000000.0;
		// now convert to unsigned long score
		unsigned long lonDate = (unsigned long)longitude;
		unsigned long latDate = (unsigned long)latitude;

		// add the longitude
		key.n0 = hash64n("gbxlongitude") ;
		// fake date
		key.n1 = lonDate;

		if ( ! dt->addKey ( &key , &score ) ) return false;

		// add the latitude
		key.n0 = hash64n("gbxlatitude") ;
		// fake date
		key.n1 = latDate;
		if ( ! dt->addKey ( &key , &score ) ) return false;
	}

	// disallow dups again
	dt->m_allowDups = false;

	// success
	return true;
}


long Events::getIntervalsNeed ( ) {
	long need = 0;
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event 
		Event *ev = &m_events[i];
		// but skip if invalidated
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		// must have an address
		if ( ! ev->m_address ) continue;
		// one key per interval (add one byte for rdbid)
		need += ev->m_ni * (sizeof(key128_t) + 1);
	}
	return need;
}

char *Events::addIntervals ( char *metaList, 
			     long long docId ,
			     char rdbId ) {
	char *pp = metaList;
	// loop over all events and hash their time intervals and their
	// address into datedb
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event 
		Event *ev = &m_events[i];
		// but skip if invalidated
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		// . THIS NOW USES TIMEDB! "tmt"
		// . see Timedb.h for key format
		pp = addIntervals2 ( ev , pp , docId , rdbId );
	}
	return pp;
}

// . each event's Event::m_intervalOff/m_ni points into the safebuf to its 
//   list of Intervals of time_t ranges for which the event occurs
// . here we hash the start time of each such Interval into datedb
char *Events::addIntervals2 ( Event *ev , 
			      char *metaList, 
			      long long docId ,
			      char rdbId ) {
	// skip if past 256 events. the datedb key puts the eventId
	// in place of the score which is only 8 bits... :(
	if ( ev->m_indexedEventId >= 256 ) return metaList;
	// shortcuts to the list of intervals this event has
	Interval *int3 = (Interval *)(ev->m_intervalsOff + m_sb.getBufStart());
	long      ni3  = ev->m_ni;
	// start and end intervals have different termid
	//long long termId1 = hash64n("gbxstart");
	//long long termId2 = hash64n("gbxend");
	char *p = metaList;
	// init "i" for the loop
	for ( long i = 0 ; i < ni3 ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// extract the left endpoint time (in seconds since epoch)
		time_t date1 = int3[i].m_a;
		time_t date2 = int3[i].m_b;
		// next start?
		long nextDate1;
		if ( i+1<ni3 ) nextDate1 = int3[i+1].m_a;
		else           nextDate1 = 0;
		// first store rdbid
		*p++ = rdbId; // RDB_TIMEDB;
		// . XmlDoc.cpp calls Events::hash to fill up a HashTableX,
		//   which is what "dt" is. then XmlDoc::addTableDate() is
		//   called which expecets the key in "dt" to be like:
		//   k.n1 = date, k.n0 = termId, and the val is 32-bit score
		*(key128_t *)p = g_timedb.makeKey(date1,
						  docId,
						  ev->m_indexedEventId,
						  date2,
						  nextDate1,
						  false);
		// advance it
		p += sizeof(key128_t);
		// . are store hours?
		// . set bit 0 on the date2 if we are store hours
		// . no, now we store them this way in getIntervals()
		//if ( ev->m_flags & EV_STORE_HOURS ) key.n1 |=  0x01;
		//else                                key.n1 &= ~0x01;
		//if ( ! dt->addKey ( &key , &score ) ) return false;
	}
	return p;
}



// . returns -1 and sets g_errno on error
// . returns NULL if no event data
char *Events::makeEventDisplay ( long *size , long *retNumDisplays ) {
	// set m_sb2 if not already set
	if ( ! m_eventDataValid ) {
		// return -1 with g_errno set on error
		if ( ! makeEventDisplay ( &m_sb2 , retNumDisplays ) ) 
			return (char *)-1;
		// validate
		m_eventDataValid = true;
	}
	// set this
	*size = m_sb2.length();	
	return m_sb2.getBufStart();
}

/////////////////////////////////////////////////////
//
// . kinda like Addresses::getAddressReply()
// . XmlDoc.cpp stores in "title rec" at ptr_eventsData/size_eventsData
// . returns NULL and sets g_errno on error
bool Events::makeEventDisplay ( SafeBuf *sb , long *retNumDisplays ) {

	// clear it out
	sb->reset();
	// make about 10k initially
	if ( ! sb->reserve ( 10000 ) ) return false;

	// printTextNorm() needs this
	m_dates->setDateParents();

	long count = 0;
	// store all the events into the "event data" blob
	for ( long i = 0 ; i < m_numEvents ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// get event 
		Event *ev = &m_events[i];
		// now do not store if we do not index it
		if ( ev->m_flags & EV_DO_NOT_INDEX ) continue;
		// skip if bad outlinked title, etc.
		//if ( ev->m_flags & EV_BAD_EVENT ) continue;
		// skip if not valid event id
		//if ( ev->m_eventId <= 0   ) continue;
		// we can only store up to 256 because the eventId is
		// limited to the 8-bit score field in the datedb key for now
		//if ( ev->m_eventId > 255 ) continue;
		// add it
		if ( ! makeEventDisplay2 ( ev , sb ) ) return false;
		// count them
		count++;
	}
	// return count
	*retNumDisplays = count;
	return true;
}

//bool Events::getEventData2 ( Event *ev , SafeBuf *sb ) {
bool Events::makeEventDisplay2 ( Event *ev , SafeBuf *sb ) {

	// access it
	long edoff = sb->length();
	// advance safebuf's cursor this many bytes
	if ( ! sb->advance ( sizeof(EventDisplay) ) ) return false;

	if ( ! m_dates->m_setDateHashes ) { char *xx=NULL;*xx=0; }
	// sanity!
	if ( ev->m_date->m_dateHash64       == 0 ) { char *xx=NULL;*xx=0; }

	// store offsets/length pairs for each description along with
	// the title and desc score

	// save start of buf
	//EventDesc *ecbuf = (EventDesc *)sb->getBuf();
	long descOff = sb->length();
	// number of event descriptions
	long numDescriptions = 0;
	// shortcuts
	long      *wlens = m_words->getWordLens();
	char     **wptrs = m_words->getWords   ();
	long       nw    = m_words->getNumWords();
	// start is first word
	char *docStart = wptrs[0];
	char *docEnd   = m_xml->getContentEnd();
	//long titleDupVotes = 0;
	bool hadTitle = false;

	// store offsets to relevant event descriptions sections
	for ( Section *si = m_sections->m_firstSent ; si ; si=si->m_nextSent){
		// breathe
		QUICKPOLL(m_niceness);
		// skip if not in description right now, no need to score it!
		if ( si->m_minEventId <= 0 ) continue;
		// get the event specific flags
		esflags_t esflags = getEventSentFlags(ev,si,m_evsft);
		// must be a sentence now
		//if ( ! ( si->m_flags & SEC_SENTENCE ) ) continue;
		// skip if overflowed. right now we only got 8 bits
		//if ( si->m_maxEventId > 255 ) continue;
		bool hasEventId = si->hasEventId ( ev->m_eventId );
		// subevent brother?
		bool subBro = ( esflags & EVSENT_SUBEVENTBROTHER );
		// . skip if not for us
		// . unless its a subevent brother in which case let it thru
		if ( ! hasEventId && ! subBro ) continue;
		//if ( ev->m_eventId < si->m_minEventId ) continue;
		//if ( ev->m_eventId > si->m_maxEventId ) continue;
		// get our event id as a byte offset and bit mask
		//unsigned char byteOff = ev->m_eventId / 8;
		//unsigned char bitMask = 1 << (ev->m_eventId % 8);
		// make sure our bit is set
		//if ( ! ( si->m_evIdBits[byteOff] & bitMask ) ) continue;
		// if we did this section already because it was split,
		// skip it then (SEC_SPLIT_SENT)
		if ( si->m_senta < si->m_a ) continue;

		// do not include tag indicators in summary ever
		if ( si->m_sentFlags & SENT_TAG_INDICATOR ) continue;
		// do not include subevent brother tags...
		if ( subBro && (si->m_sentFlags & SENT_TAGS) ) continue;
		     

		// . ignore before event title
		// . this will strike out event desc sentences when printing
		//   them out if this function returns false
		// . allow them through just so turk might select as title?
		//esflags_t esflags=getEventSentFlags (ev,si);
		// skip if not indexable
		//if (!(esflags & EVSENT_IS_INDEXABLE ))continue;

		// do not print if descScore is 0 either and not title.
		// that means it had SEC_MENU or SEC_MENU_HEADER set. it
		// was still considered as a title candidate, but unless it
		// was selected as the title we completely ignore it.
		// this fixes southgatehouse.com which has titles that are
		// a few consecutive links of the bands playing that night.
		// they are still marked as SEC_MENU but they can be the title
		// now, and since they do not get TF_PAGE_REPEAT set because
		// they are unique, they make it as the title.
		//if ( si->m_descScore == 0.0 &&
		//     // allow if title though for this event
		//     ev->m_titleSection != si ) continue;
		//if ( dscore == 0.0 ) continue;

		// print it out
		long a = si->m_senta;
		long b = si->m_sentb;

		// skip over it
		if ( ! sb->advance(sizeof(EventDesc)) ) return false;
		// point to it
		EventDesc *ec = (EventDesc *)(sb->getBuf()-sizeof(EventDesc));
		// count it
		numDescriptions++;
		// include the trailing punctuation mark, if any
		char *end = wptrs[b-1] + wlens[b-1] ;
		char *max = docEnd;
		if ( b < nw ) max = wptrs[b] + wlens[b];
		// save it
		char *origEnd = end;
		// . sometimes they have a " )" so gotta scan full punct word
		// . sometimes they have a sequence of two punct chars:
		//   Need a place to stay while you are in town for
		//   "A tuna christmas?"
		bool gotOne = false;
		for ( ; end < max ; end++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			if ( is_wspace_a(*end) ) continue;
			if ( *end == '.' ||
			     *end == '?' ||
			     *end == '!' ||
			     *end == ')' ||
			     *end == ']' ||
			     // "A Tuna Christmas"
			     *end == '\"'||
			     *end == '\'') {
				gotOne = true;
				continue;
			}
			//*end == ';' ||
			//*end == ':' ||
			// break on anything else!
			break;
		}
		// if didn't get one, do not include spaces
		if ( ! gotOne ) end = origEnd;
		// likewise, if beginning starts with a quote or $
		char *start = wptrs[a];
		if ( start > docStart && 
		     ( start[-1] == '$' || 
		       start[-1]=='\'' || 
		       start[-1]=='\"' || 
		       start[-1]=='('  ||
		       start[-1]=='['   ))
			start--;
		// then set it
		ec->m_off1 = start - docStart;
		// set end of it
		ec->m_off2 = end - docStart;

		// we have to use sentence content hash so that sentences
		// who consist of multiple sections do not have a 0 content
		// hash. so now we have to make sure to update all the other
		// code related to this!
		ec->m_sentContentHash32 = si->m_sentenceContentHash;
		ec->m_tagHash32 = si->m_turkTagHash;

		// sanity. if this ever happens legitmately then set to 1
		// but for now keep this here to debug
		if ( ec->m_sentContentHash32 == 0 ) { char *xx=NULL;*xx=0; }

		/*
		// . compute the turk tag hash
		// . hash of the last 5 parent tags for this sentence
		uint32_t turkTagHash = 0;
		Section *ps = si;
		long pcount = 0;
		for ( ; ps ; ps = ps->m_parent ) {
			// only 5 parents max!
			if ( pcount++ >= 5 ) break;
			// accumlate
			turkTagHash = hash32h ( turkTagHash , ps->m_tagId );
			// mod
			turkTagHash <<= 1;
		}
		ec->m_turkTagHash5 = turkTagHash;
		*/

		// set this too
		ec->m_alnumPosA = si->m_alnumPosA;
		ec->m_alnumPosB = si->m_alnumPosB;
		// clear flags
		ec->m_dflags = 0;
		// mark the tags
		if ( si->m_sentFlags & SENT_TAGS ) ec->m_dflags |= EDF_TAGS;
		// sanity check
		if ( ec->m_alnumPosA == ec->m_alnumPosB ){char *xx=NULL;*xx=0;}
		// mix it up!
		uint32_t evkey = hash32h((uint32_t)ev,12345);
		uint64_t key = (((uint64_t)si)<<32) | (uint32_t)evkey;

		// set title flag or whatever
		//Section *ts = ev->m_titleSection->m_sentenceSection;
		// if has title, that is good!
		if ( ev->m_titleStart >= 0 &&
		     si->contains2 ( ev->m_titleStart ) ) {
			// flag it for display as title
			ec->m_dflags |= EDF_TITLE;
			// is it in a <title> tag?
			Section *ps = si;
			for ( ; ps ; ps = ps->m_parent ) {
				QUICKPOLL(m_niceness);
				if ( ps->m_tagId == TAG_TITLE ) break;
			}
			if ( ps ) ec->m_dflags |= EDF_INTITLETAG;
			// flag this
			hadTitle = true;
			//
			// set these
			//titleDupVotes = si->m_votesForDup;
		}

#ifdef _USETURKS_
		/*
		// get turk votes
		turkbits_t *tbp = (turkbits_t *)m_tbt->getValue (&key);
		// confirmed candidate? if it was in a section that
		// had a winning indirect vote score, but multiple
		// sentences also had the same tag hash, consider it
		// a "confirmed candidate" title. not as powerful as
		// a "confirmed title" outright, but pretty good. the
		// algo picked it as the title and it happened to 
		// match the tag hash indirectly voted on by the turk,
		// in the case of 2 or more sentence matching the
		// tag hash as well.
		if ( tbp && (*tbp & TB_TITLE_CANDIDATE) )
			ec->m_dflags |= EDF_TURK_TITLE_CANDIDATE;

		if ( tbp && (*tbp & TB_TITLE) )
			ec->m_dflags |= EDF_TURK_TITLE;
		*/
#endif

		// . confirmed?
		// . we no longer require the sentence to contain the
		//   Event::m_titleStart for setting EDF_CONFIRMED_TITLE
		//if ( ev->m_confirmedTitle &&
		//     si->m_sentenceContentHash == 
		//     ev->m_confirmedTitleContentHash32 ) {
		//	ec->m_dflags |= EDF_CONFIRMED_TITLE;
		//	// this can cause title dups
		//	ec->m_dflags |= EDF_TITLE;
		//}

		// can't * these since subeventbrothers are not in table
		float *tscorep = (float *)m_titleScoreTable.getValue ( &key );
		float *dscorep = (float *)m_descScoreTable.getValue ( &key );
		//sentflags_t *sfp;
		//sfp =(sentflags_t *)m_titleFlagsTable.getValue(&key);

		if ( esflags & EVSENT_SECTIONDUP ) {
			// rewind it!
			sb->advance ( -1 * sizeof(EventDesc) );
			// uncount
			numDescriptions--;
			// be gone
			continue;
			//ec->m_dflags |= EDF_MENU_CRUFT;
		}
			

		//if ( si->contains ( ts ) ) ec->m_titleScore = 100.0;
		//else                       ec->m_titleScore = 000.0;

		// TODO: mark phone, email, generics plus those

		//
		// check turk table to see if confirmed descr/not descr
		//
		//uint64_t tbkey = (((uint64_t)si)<<32) | (uint32_t)ev;
		// get its turkbits
		//turkbits_t *tbp = (turkbits_t *)m_tbt->getValue ( &key );
		// shortcut
		//turkbits_t tb = 0;
		// assign if we had them
		//if ( tbp ) tb = *tbp;
#ifdef _USETURKS_
		/*
		// . mod dscore based on turk votes
		// . maybe the tag hash was voted to not be part of desc...
		if ( tbp && (*tbp & TB_DESCR) ) 
			ec->m_dflags |= EDF_CONFIRMED_DESCR;
		// exclude tag overrides
		if ( tbp && (*tbp & TB_NOT_DESCR) ) 
			ec->m_dflags|=EDF_CONFIRMED_NOT_DESCR;
		*/
#endif


		// . mark this though!
		// . BUT only for highlighting in the cached page!
		//if ( *sfp & (SENT_GENERIC_PLUS_PLACE | SENT_PLACE_NAME) )
		//	ec->m_dflags |= EDF_HASEVENTADDRESS;

		if ( (si->m_sentFlags & SENT_HAS_PRICE) &&
		     // fix "School Board OKs $50M in Projects" for 
		     // legals.abqjournal.com
		   !(si->m_flags&(SEC_MENU|SEC_MENU_HEADER|SEC_MENU_SENTENCE)))
			ec->m_dflags |= EDF_HAS_PRICE;

		if ( esflags & EVSENT_SUBEVENTBROTHER )
			ec->m_dflags |= EDF_SUBEVENTBROTHER;

		if ( esflags & EVSENT_HASEVENTADDRESS )
			ec->m_dflags |= EDF_HASEVENTADDRESS;

		if ( (esflags & EVSENT_GENERIC_WORDS) &&
		     (esflags & EVSENT_HASEVENTADDRESS) )
			ec->m_dflags |= EDF_JUST_PLACE;

		if ( esflags & EVSENT_HASEVENTDATE )
			ec->m_dflags |= EDF_HASEVENTDATE;

		if ( si->m_sentFlags & SENT_PRETTY )
			ec->m_dflags |= EDF_PRETTY;

		if ( esflags & EVSENT_IS_INDEXABLE )
			ec->m_dflags |= EDF_INDEXABLE;

		if ( esflags & EVSENT_FARDUP )
			ec->m_dflags |= EDF_FARDUP;

		if ( esflags & EVSENT_FARDUPPHONE )
			ec->m_dflags |= EDF_FARDUPPHONE;

		if ( esflags & EVSENT_FARDUPPRICE )
			ec->m_dflags |= EDF_FARDUPPRICE;

		// shouldn't the title score be already set?
		ec->m_titleScore = 0;
		ec->m_descScore  = 0;
		if ( tscorep ) ec->m_titleScore = *tscorep;
		if ( dscorep ) ec->m_descScore  = *dscorep;

		// facebook image url?
		if ( si->m_parent && si->m_parent->m_tagId==TAG_FBPICSQUARE ) {
			ec->m_dflags |= EDF_FBPICSQUARE;
			ec->m_descScore = 100;
		}

	}

	if ( ! hadTitle ) { 
		log("event: had no title");
		//char *xx=NULL;*xx=0;
	}

	/////////////////////////
	//
	// store more place names
	//
	/////////////////////////
	char ppdbuf[1024];
	HashTableX ppdedup;
	ppdedup.set(8,0,64,ppdbuf,1024,false,m_niceness,"ppdbuf");
	//HashTableX tagCounts;
	//char tcbuf[1024];
	//tagCounts.set(4,4,64,tcbuf,1024,false,m_niceness,"tcbuf");
	PlaceMem *PM;
	// num Place-based EventDesc  added
	long numAdded = 0;
	// first process the "streets"
	PM = &m_addresses->m_sm;
	// bookmark the start for reprocessing loop after this one
	//long ecstartOff = sb->length();
	// count the big loops
	long bigCount = 0;
 bigloop:
	// if place name is 'after at' or in an address->m_name1|2 field OR
	// has an indicator in it then make a separate EventDesc for it and 
	// set its EDF_PLACE_TYPE flag.
	// . scan places
	for ( long i = 0 ; i < PM->getNumPtrs() ; i++ ) {
		// breathe
		QUICKPOLL ( m_niceness );
		// get it
		Place *pp = (Place *)PM->getPtr(i);
		// . is it a "fake" street? (PM = m_sm)
		// . then it will be in the m_sm list (street list)
		if ( pp->m_type == PT_STREET ) {
			bool isName  = pp->m_flags2 & PLF2_IS_NAME ;
			// if we are a building/venue name isName will be true
			if ( ! isName ) continue;
		}
		// . is it a place name? (PM = m_pm)
		// . then it will be in the m_pm list
		if ( pp->m_type != PT_STREET &&
		     pp->m_type != PT_NAME_1 &&
		     pp->m_type != PT_NAME_2 ) 
			continue;
		// skip if not in doc
		if ( pp->m_a < 0 ) continue;
		// get full sentence
		Section *si = m_sections->m_sectionPtrs[pp->m_a];
		// make it sentence
		si = si->m_sentenceSection;
		// 1. must have our event id
		if ( ! si->hasEventId(ev->m_eventId) ) 
			continue;

		// dedup place names since same name in m_sm can repeat
		// in m_pm
		//if (   ppdedup.isInTable ( &pp->m_wordHash64 ) ) continue;

		// 2. must have indicator or afterat or addressptr
		bool good = false;
		// good if full sentence is it
		if ( pp->m_a==si->m_a && pp->m_b==si->m_b) good = true;
		// good if in address, even unverified
		if ( pp->m_unverifiedAddress ) good = true;
		// good if after at
		if ( pp->m_flags2 & PLF2_AFTER_AT ) good = true;
		// if place is just a stopword like "the" then the
		// m_wordHash64 will be zero! i guess we do not hash
		// stop words like 'the' so that 'the tavern' equals
		// 'tavern'...
		if ( pp->m_wordHash64 == 0LL ) good = false;
		// TODO: add this somehow... 
		//if ( pp->m_flags2 & PLF2_HAD_INDICATOR ) good = true;
		// skip if no good
		if ( ! good ) continue;

		// this deduping was causing us to miss a TB_TURK or
		// TB_TURK_CANDIDATE turk vote because for some reason
		// it was only for one place name and not the other...
		// even though they had the same m_a/m_b... so they would

		// make a key
		uint64_t key = ((uint64_t)pp->m_a)<<32 | ((uint64_t)pp->m_b);
		// dedup between pm and sm with that for now
		if ( ppdedup.isInTable ( &key ) ) continue;
		// add it always
		if ( ! ppdedup.addKey ( &key ) ) return false;

		// . now count the taghash for each place
		// . so we know what place name candidates have a unique 
		//   tag hash
		// . in the case of two place names being the same content
		//   hash, we will pick the one with the unique tag hash
		//   to be in the drop down if possible
		//--- no, just make a venu2 and venu3 tags maybe like we do with title
		//if ( ! tagCounts.addTerm32 (&si->m_turkTagHash)) 
		//	return false;

		// HACK: save this
		pp->m_eventDescOff = sb->length();
		// skip over it
		if ( ! sb->advance(sizeof(EventDesc)) ) return false;
		// point to it
		EventDesc *ec = (EventDesc *)(sb->getBuf()-sizeof(EventDesc));
		// count them 
		numAdded++;
		// count it
		numDescriptions++;
		// include the trailing punctuation mark, if any
		char *end = wptrs[pp->m_b-1] + wlens[pp->m_b-1] ;
		// likewise, if beginning starts with a quote or $
		char *start = wptrs[pp->m_a];
		// then set it
		ec->m_off1 = start - docStart;
		// set end of it
		ec->m_off2 = end - docStart;
		// we have to use sentence content hash so that sentences
		// who consist of multiple sections do not have a 0 content
		// hash. so now we have to make sure to update all the other
		// code related to this!
		ec->m_sentContentHash32 = (unsigned long)pp->m_wordHash64;
		// use a non-zero tag hash only iff we are the full sentence
		//if ( si->m_a == pp->m_a && si->m_b == pp->m_b )
		// no, no, its ok since we are now restricting ourselves
		// to just place names for TB_VENUE bits. so if there are 2+
		// place names that both have this tag hash we will not use
		// either one. but if there is only one, then we will use
		// that as the turk confirmed venue.
		ec->m_tagHash32 = si->m_turkTagHash;
		// otherwise, set it to zero so not used for indirect voting
		//else 
		//	ec->m_tagHash32 = 0;
		// sanity. if this ever happens legitmately then set to 1
		// but for now keep this here to debug
		if ( ec->m_sentContentHash32 == 0 ) { char *xx=NULL;*xx=0; }
		// set this too
		ec->m_alnumPosA = si->m_alnumPosA;
		ec->m_alnumPosB = si->m_alnumPosB;
		// sanity check
		if ( ec->m_alnumPosA == ec->m_alnumPosB ){char *xx=NULL;*xx=0;}
		// shouldn't the title score be already set?
		ec->m_titleScore = 0;
		ec->m_descScore  = 0;
		// use this to hold a ptr to the place for now (HACK)
		*(long *)(&ec->m_descScore) = (long)pp;
		// set it
		ec->m_dflags = EDF_PLACE_TYPE;
		// . best venue name might not be set!
		// . this is a dedup issue
		if ( ev->m_confirmedVenue &&
		     ev->m_confirmedVenueContentHash32 == 
		     (unsigned long)pp->m_wordHash64 ) {
			ec->m_dflags |= EDF_BEST_PLACE_NAME;
			// now we have this, too, in addition to CF_CONFIRMED..
			//ec->m_dflags |= EDF_CONFIRMED_VENUE;
			// . sanity test
			// . best place should be the one the turks confirmed!
			if ( ev->m_bestVenueName &&
			     ev->m_bestVenueName->m_wordHash64 !=
			     pp->m_wordHash64 ) { char *xx=NULL;*xx=0; }
		}
		// but if this is the one we ultimately chose, mark it
		// some more with EDF_BEST_PLACE_NAME
		if ( ev->m_bestVenueName &&
		     ev->m_bestVenueName->m_a == pp->m_a &&
		     ev->m_bestVenueName->m_b == pp->m_b )
			ec->m_dflags |= EDF_BEST_PLACE_NAME;
#ifdef _USETURKS_
		/*
		// was it turked?
		uint64_t tbkey = (((uint64_t)pp)<<32) | (uint32_t)ev;
		// get turk votes
		turkbits_t *tbp = (turkbits_t *)m_tbt->getValue (&tbkey);
		// not there?
		if ( tbp && (*tbp & TB_VENUE) )
			ec->m_dflags |= EDF_TURK_VENUE;
		if ( tbp && (*tbp & TB_VENUE_CANDIDATE) )
			ec->m_dflags |= EDF_TURK_VENUE_CANDIDATE;
		*/
#endif

	}
	// then process the "names"
	PM = &m_addresses->m_pm;
	// make sure this is the 2nd time around before going back up
	if ( ++bigCount == 1 ) goto bigloop;

	/*
	// content hash dedup
	char chbuf[1024];
	HashTableX chdedup;
	chdedup.set(8,4,64,chbuf,1024,false,m_niceness,"chbuf");

	// point to beginning of the Place-based EventDesc we added
	EventDesc *ecstart = (EventDesc *)(sb->getBufStart() + ecstartOff);

	// loop over the places we added that had unique [a,b] (ppdedup)
	for ( long i = 0 ; i < numAdded ; i++ ) {
		// breathe
		QUICKPOLL(m_niceness);
		// shortcut
		EventDesc *pe = &ecstart[i];
		// a secret ptr (HACK)
		Place *pp = *(Place **)(&pe->m_descScore);
		// get full sentence
		Section *si = m_sections->m_sectionPtrs[pp->m_a];
		// make it sentence
		si = si->m_sentenceSection;
		// who is the current leading place for this contenthash?
		Place **rp =(Place **)chdedup.getValue ( &pp->m_wordHash64 );
		Place  *rival = NULL; 
		if ( rp ) rival = *rp;
		// if we are the first, just claim it
		if ( ! rival ) {
			if ( ! chdedup.addKey ( &pp->m_wordHash64 , &pp ) )
				return false;
			continue;
		}
		// . "pd" is the place that had this m_wordHash64 last so far
		// . get his sentence
		Section *rs = m_sections->m_sectionPtrs[rival->m_a];
		// make sure sentence, the rival's sentence
		rs = rs->m_sentenceSection;
		// is he the only place with the tag hash he has?
		long sc1 = tagCounts.getScore32 ( &rs->m_turkTagHash );
		// and us
		long sc2 = tagCounts.getScore32 ( &si->m_turkTagHash );
		// if he has unique tag hash leave him be
		if ( sc1 <= 1 ) continue;
		// get rival's eventsentflags
		esflags_t esflags = getEventSentFlags(ev,rs,m_evsft);
		// if we do nto hae a unqiue tag hash either, then
		// see if he is above a street. if he is then that is
		// pretty good!
		if ( sc2 > 1 && (esflags & EVSENT_NAMEABOVESTREET) ) continue;
		// ptr to it
		char *rdata = sb->getBufStart() + rival->m_eventDescOff;
		// use offset for safebuf
		EventDesc *rdesc = (EventDesc *)rdata;
		// otherwise, he was the dup and we override
		// but rather than put the eventdesc out of order
		// just set a flag for him. then check for EDF_DUP
		// in XmlDoc::getTurkForm() so we do not display it
		// in the dropdown venue menu! (HACK)
		rdesc->m_dflags |= EDF_FORMAT_DUP;
		// . add it to avoid dups now
		// . will overwrite if already in table
		if ( ! chdedup.addKey ( &pp->m_wordHash64, &pp ) )return false;
	}
	*/

	long startLen;

	///////////////////////////////////
	//
	// store event start intervals
	//
	startLen = sb->length();
	// point to the interval for this event
	Interval *int3 ;
	int3 = (Interval *)(ev->m_intervalsOff + m_sb.getBufStart());
	long ni3 = ev->m_ni;
	if(!sb->safeMemcpy ((char *)int3,ni3*sizeof(Interval))) return false;
	// assign the offsets
	long intOff  = startLen;
	long intSize = sb->length() - startLen;

	///////////////////////////////////
	//
	// store event address(es)
	//
	startLen = sb->length();
	// get its address to serialize it
	Address *aa = ev->m_address;
	// . serialize it into "ap"
	// . TODO: make sure only uses verified place names!
	if ( ! aa->serializeVerified ( sb ) ) return false;
	// ensure included \0
	//if ( ap[bytes-1] != '\0' ) { char *xx=NULL;*xx=0; }
	// assign the offsets
	long addrOff  = startLen;
	long addrSize = sb->length() - startLen;
	

	///////////////////////////////////
	//
	// store normalized event date for printing out
	//
	startLen = sb->length();
	// store it
	if ( ! ev->m_date->printTextNorm (sb,m_words,false,ev,&m_sb) )
		return false;
	// push a \0 on there if we need to
	if ( sb->getBuf()[-1] != '\0' && ! sb->pushChar('\0') ) return false;
	// assign the offsets
	long normDateOff  = startLen;
	long normDateSize = sb->length() - startLen;


	///////////////////////////////////
	//
	// set "ed" now
	//
	EventDisplay *ed = (EventDisplay *)(sb->getBufStart() + edoff);
	// sanity
	if ( ev->m_indexedEventId <= 0   ) { char *xx=NULL;*xx=0; }
	if ( ev->m_indexedEventId >  256 ) { char *xx=NULL;*xx=0; }
	// fake it out. the ptrs are actually offsets for storing
	ed->m_indexedEventId  = ev->m_indexedEventId;
	ed->m_numDescriptions = numDescriptions;

	ed->m_addressHash64   = ev->m_address->m_hash;

	// this should be normalized
	ed->m_dateHash64  = ev->m_dateHash64;//->m_dateHash;
	ed->m_titleHash64 = ev->m_titleHash64;
	ed->m_eventHash64 = ev->m_eventHash64;

	// copy over the tag hashes too
	ed->m_dateTagHash32  = ev->m_dateTagHash32;
	ed->m_addressTagHash32 = ev->m_addressTagHash32;
	ed->m_titleTagHash32 = ev->m_titleTagHash32;

	ed->m_adch32 = ev->m_adch32;
	ed->m_adth32 = ev->m_adth32;
	//ed->m_eventTagHash32 = ev->m_eventTagHash32;

	// now we store the flags from the event so we can show them
	// in the search results. like EV_DEDUPED, EV_OUTLINKED_TITLE
	// or EV_OLD_EVENT or EV_STORE_HOURS, etc. we store such bad events
	// if they were directly turked so as not to lose the turk voting
	// information.
	ed->m_eventFlags = ev->m_flags;

	// the lat/lon we lookedup from the geocoder. will be NO_LATITUDE
	// if is invalid.
	ed->m_geocoderLat = ev->m_address->m_geocoderLat;
	ed->m_geocoderLon = ev->m_address->m_geocoderLon;

	// sanity, must exist!
	//if ( ! m_xd->ptr_eventTagBuf ) { char *xx=NULL;*xx=0; }

	// . make it an offset into XmlDoc::ptr_eventTagBuf
	// . comma-separated, null termianted, list of tag words/phrases
	//ed->m_tagsPtr = ev->m_tagsPtr - (long)m_xd->ptr_eventTagBuf;
	// sanity
	//if ( (long)ed->m_tagsPtr < 0 ) { char *xx=NULL;*xx=0; }

	// propagate the store hours flag so we can display the countdown
	// in a "store hours" format on the search results page. i.e.
	// show "Closes in ten minutes" instead of "Starts in 1 day..."
	//if ( ev->m_flags & EV_STORE_HOURS )
	//	ed->m_edflags |= EDIF_STORE_HOURS;

	ed->m_desc            = (EventDesc *)descOff;
	ed->m_addr            = (char *)addrOff;
	ed->m_int             = (long *)intOff;
	ed->m_normDate        = (char *)normDateOff;

	if ( intSize <= 0 ) { char *xx=NULL;*xx=0; }

	ed->m_descSize  = numDescriptions * sizeof(EventDesc);
	ed->m_addrSize  = addrSize;
	ed->m_intSize   = intSize;
	ed->m_normDateSize = normDateSize;

	ed->m_confirmed = 0;
	if ( ev->m_confirmedAccept ) ed->m_confirmed |= CF_ACCEPT;
	if ( ev->m_confirmedReject ) ed->m_confirmed |= CF_REJECT;
	if ( ev->m_confirmedTitle  ) ed->m_confirmed |= CF_TITLE;
	if ( ev->m_confirmedVenue  ) ed->m_confirmed |= CF_VENUE;

	if ( ev->m_confirmedVenue &&
	     ev->m_confirmedVenueContentHash32 == 0 ) 
		ed->m_confirmed |= CF_VENUE_NONE;


	static bool s_flag = false;
	static long long h_thru;
	static long long h_through;
	static long long h_to;
	static long long h_and;
	static long long h_on;
	static long long h_from;
	static long long h_at;
	static long long h_until;
	static long long h_til;
	static long long h_till;

	if ( ! s_flag ) {
		s_flag = true;
		h_thru = hash64n("thru");
		h_through = hash64n("through");
		h_to = hash64n("to");
		h_and = hash64n("and");
		h_on = hash64n("on");
		h_from = hash64n("from");
		h_at = hash64n("at");
		h_until = hash64n("until");
		h_til = hash64n("til");
		h_till = hash64n("till");
	}

	// . flatten the dates out into individual date elements
	// . this is recursive and defined in Dates.cpp
	//Date *all[1024];
	//long numAll = 0;
	//addPtrToArray ( all , &numAll , ev->m_date , NULL );
	long ne = 0;
	Date **delms = m_dates->getDateElements ( ev->m_date , &ne );
	long nd = 0;
	Date *last = NULL;
	for ( long i = 0 ; i < ne ; i++ ) {
		// get the date element
		Date *de = delms[i];
		// get the prev
		Date *prev = last;
		// update last
		last = de;
		// get prev date end
		if ( prev ) {
			// scan if words between us are bridgeable
			bool good = true;
			long count = 0;
			for ( long j = prev->m_b ; j < m_nw ; j++ ) {
				QUICKPOLL(m_niceness);
				// do not allow more than 20 words in between
				if ( ++count >= 20 ) { good = false; break; }
				// got next date? we made it!
				if ( j == de->m_a ) { good = true; break; }
				// skip over punct or tags
				if ( ! m_wids[j] ) continue;
				// skip unless through, thru, etc
				if ( m_wids[j] == h_thru ) continue;
				if ( m_wids[j] == h_through ) continue;
				if ( m_wids[j] == h_to ) continue;
				if ( m_wids[j] == h_and ) continue;
				if ( m_wids[j] == h_on ) continue;
				if ( m_wids[j] == h_from ) continue;
				if ( m_wids[j] == h_at ) continue;
				if ( m_wids[j] == h_until ) continue;
				if ( m_wids[j] == h_til ) continue;
				if ( m_wids[j] == h_till ) continue;
				// otherwise, we have to stop
				good = false;
				break;
			}
			if ( good ) {
				// union this date in
				ed->m_dateEnds[nd-1] = de->m_b;
				// and be on our way
				continue;
			}
		}
		// add it
		ed->m_dateStarts[nd] = de->m_a;
		ed->m_dateEnds  [nd] = de->m_b;
		nd++;
		// update count
		ed->m_numDates = nd;
		// stop to prevent breach
		if ( nd >= MAX_ED_DATES ) break;
	}
	// make into offsets
	for ( long i = 0 ; i < nd ; i++ ) {
		long a = ed->m_dateStarts[i];
		long b = ed->m_dateEnds  [i];
		ed->m_dateStarts[i] = wptrs[a] - docStart;
		ed->m_dateEnds  [i] = wptrs[b-1] + wlens[b-1] - docStart;
	}

	// . set the date offset information for up to 3 pieces of the date
	// . these are failing us
	/*
	for ( long i = 0 ; i < MAX_ED_DATES ; i++ ) {
		// get date offsets
		bool found = m_dates->getDateOffsets ( ev->m_date           ,
						       i                    ,
						       &ed->m_dateStarts[i] ,
						       &ed->m_dateEnds  [i] ,
						       NULL                 ,
						       NULL                 );
		if ( ! found ) break;
		ed->m_numDates = i+1;
	}
	*/

	// for skipping quickly over ed's
	ed->m_totalSize = sizeof(EventDisplay) +
		// now just store the offsets, not the strings
		ed->m_descSize  + 
		ed->m_addrSize  +
		ed->m_normDateSize +
		ed->m_intSize   ; 

	// success
	return true;
}

// . which sentence sections should we keep and which should we toss?
// . be very strict to get the best descriptions
bool Events::isIndexable ( Event *ev , Section *si ) {

	// sanity check
	if ( ! m_bitsOrig->m_inLinkBitsSet ) { char *xx=NULL;*xx=0;}

	Section *ts = ev->m_titleSection->m_sentenceSection;
	// if has title, that is good!
	if ( si->contains ( ts ) ) return true;

#ifdef _TURKUSER_
	/*
	// turk votes override!
	uint64_t tbkey = (((uint64_t)si)<<32) | (uint32_t)ev;
	// get its turkbits
	turkbits_t *tbp = (turkbits_t *)m_tbt->getValue ( &tbkey );
	// shortcut
	turkbits_t tb = 0;
	// assign if we had them
	if ( tbp ) tb = *tbp;
	// venue wins. this requires hash of place name (Place::m_wordHash64)
	// not "si"
	//if ( tb & TB_VENUE ) return true;
	// exclude tag overrides
	if ( tb & TB_NOT_DESCR ) return false;
	*/
#endif

	sentflags_t sflags = si->m_sentFlags;

	// get the event specific flags
	esflags_t esflags = getEventSentFlags(ev,si,m_evsft);

	// if another sentence has exactly the same content as this one
	// but it is closer to Event::m_date->m_mostUniquePtr then do not
	// index the dup! it won't hurt, but the turk gui uses 
	// INDEX_DO_NOT_INDEX to decide whether to check the checkbox in
	// the event description list.
	if ( esflags & EVSENT_FARDUP ) return false;

	// skip if all generics
	//if ( sflags & SENT_GENERIC_WORDS ) return false;

	// the problem with SENT_IS_DATE is is that we get "holiday" which has
	//  a dscore of 0 but it has this flag set... 
	// only set flag for our date...
	// provide the date in order, like being after or before the title
	// sometimes matters.
	if ( (esflags & EVSENT_HASEVENTDATE) && 
	     // if its only date words, skip it though
	     ! (esflags & EVSENT_JUSTDATES) )
		return true; // put back
	// keep address now too so we can announce that like we announce dates
	if ( esflags & EVSENT_HASEVENTADDRESS ) return true;
	// sentence containing date is good...?
	//if ( sflags & SENT_HASSOMEEVENTSDATE ) return true;


	if ( esflags & EVSENT_FARDUPPHONE ) return false;
	if ( esflags & EVSENT_FARDUPPRICE ) return false;

#ifdef _USETURKS_
	/*
	// . maybe the tag hash was voted to not be part of desc...
	// . move this down since might be a FARDUP because the turk bits
	//   will be set on all sentences matching
	if ( tb & TB_DESCR ) return true;

	// title wins
	if ( tb & TB_TITLE ) return true;
	*/
#endif

	// . always have phone
	// . EVSENT_FARDUPPHONE is set if another phone number containing 
	//   sentence exists that is topologically closer to the event date
	//   Event::m_date->m_mostUniquePtr.
	if ( sflags & SENT_HAS_PHONE ) return true;
	// if has price that's ok
	if ( sflags & SENT_HAS_PRICE ) return true; 


	// assume everything in xml is indexable, unless its a plain
	// decimal or hex number or url or date
	if ( m_xd->m_contentType == CT_XML ) {
		// assume ignore it
		bool indexable = false;
		long generics = 0;
		// scan words in the sentence
		for ( long i = si->m_a ; i < si->m_b ; i++ ) {
			// breathe
			QUICKPOLL(m_niceness);
			// skip if not word
			if ( ! m_wids[i] ) continue;
			// check them out
			if ( m_bits[i] & D_IS_IN_DATE ) continue;
			if ( m_bits[i] & D_IS_MONTH   ) continue;
			if ( m_bits[i] & D_IS_IN_URL  ) continue;
			if ( m_bits[i] & D_IS_NUM     ) continue;
			if ( m_bits[i] & D_IS_HEX_NUM ) continue;
			// . a single generic word is bad
			// . to avoid stuff like <blah>yes</blah>
			if ( (m_bits[i] & D_CRUFTY) && ++generics <= 1 )
				continue;
			// ok, index it i guess
			indexable = true;
		}
		return indexable;
	}


	// if it is a sentence before the title and not a recognized
	// category then do not index it
	if ( si->m_b <= ts->m_a ) return false;

	// do not even index if a menu
	if ( si->m_flags & (SEC_MENU | SEC_MENU_HEADER | SEC_MENU_SENTENCE))
		return false;

	// is it like "Tags:" ?
	if ( sflags & SENT_TAG_INDICATOR ) return false;

	// are we MENU_CRUFT?
	//if ( sflags & SENT_DUP_SECTION ) return false;

	/*
	if ( sflags & SENT_IN_MENU ) return false;
	if ( sflags & SENT_IN_MENU_HEADER ) return false;
	if ( sflags & SENT_NUKE_FIRST_WORD ) return false;
	if ( (sflags & SENT_PAGE_REPEAT) && 
	     (sflags & menu) &&
	     (sflags & SENT_MIXED_CASE) ) return false;
	if ( (sflags & SENT_MULT_EVENTS) && 
	     (sflags & menu) &&
	     (sflags & SENT_MIXED_CASE) ) return false;
	*/

	// mix it up!
	uint32_t evkey = hash32h((uint32_t)ev,12345);
	// hash them up
	uint64_t key = (((uint64_t)si)<<32) | (uint32_t)evkey;
	float *dp = (float *)m_descScoreTable .getValue ( &key );
	// not indexable if dscore was set to 0.0
	if ( dp && *dp == 0.0 ) return false;

	// . if not a pretty sentence, then not indexable
	// . UNTIL we get category sentences!
	if ( sflags & SENT_PRETTY ) return true;

	// if its stubhub we want to index the keywords
	if ( ! m_isStubHubValid ) { char *xx=NULL;*xx=0; }
	if (   m_isStubHub      ) return true;
	if (   m_isEventBrite   ) return true;

	// forget it if not pretty
	return false;
}


bool cacheEventLatLons ( char     *ptr_eventsData  ,
			 long      size_eventsData ,
			 RdbCache *latLonCache     ,
			 long      niceness        ) {
	
	char *p    = ptr_eventsData;
	char *pend = ptr_eventsData + size_eventsData;
	// scan them
	for ( ; p < pend ; ) {
		// breathe
		QUICKPOLL(niceness);
		// cast it
		EventDisplay *ed = (EventDisplay *)p;
		// point to serialized addr
		//char *addr  = (char *)((long)ed->m_addr  + ptr_eventsData);
		// skip this event display blob
		p += ed->m_totalSize;
		// if no lat/lon bail.. need to figure out why so
		// many address have 999/999 !!!
		if ( ed->m_geocoderLat == 999 ) continue;
		if ( ed->m_geocoderLon == 999 ) continue;
		// get the lat lon and cache that
		long long key64 = (long long)ed->m_addressHash64;
		// wtf?
		if ( key64 == 0LL ) continue;
		// make the record
		double recs[2];
		recs[0] = ed->m_geocoderLat;
		recs[1] = ed->m_geocoderLon;
		// add to cache
		latLonCache->addRecord((collnum_t)0,
				       (char *)&key64,
				       (char *)recs,
				       sizeof(double)*2 );
	}
	return true;
}

// . this is the blob in XmlDoc::ptr_eventData
// . store each EventDisplay as a variable sized chunk so we can put
//   the address and the start times right after it, \0 separated, that
//   way msg20 can send the whole blob back as one thing
// . EventDisplay::m_totalSize is the size of the returned ptr
EventDisplay *getEventDisplay ( long  indexedEventId         ,
				char *ptr_eventsData  , 
				long  size_eventsData ) {
				//char *ptr_eventTagBuf ) {
	// sanity
	if ( indexedEventId <= 0 ) { char *xx=NULL;*xx=0; }
	char *p    = ptr_eventsData;
	char *pend = ptr_eventsData + size_eventsData;
	// scan them
	for ( ; p < pend ; ) {
		// cast it
		EventDisplay *ed = (EventDisplay *)p;
		// check it, return if match
		if ( ed->m_indexedEventId != indexedEventId ) {
			// skip this event display blob
			p += ed->m_totalSize;
			// and get the next one in line
			continue;
		}
		// . if already deserialized skip this part
		// . this prevents core on repeat calls on same evid
		if ( ed->m_eventFlags & EV_DESERIALIZED ) return ed;
		// shift down
		/*
		if ( ! g_repair.m_completed ) {
			char *src = ((char *)(&ed->m_eventFlags))+4;
			char *dst = src + 4;
			long toCopy = ed->m_totalSize - 4 - 4 - 4;
			memmove ( dst , src , toCopy );
			ptr_eventsData += 4;
		}
		*/
		// comma-separated (null terminated) list of tag words/phrases
		//ed->m_tagsPtr = ptr_eventTagBuf + (long)ed->m_tagsPtr;
		// ok, transform the offsets into ptrs
		ed->m_desc  = (EventDesc *)((long)ed->m_desc  +ptr_eventsData);
		ed->m_addr  = (char *)((long)ed->m_addr  + ptr_eventsData);
		ed->m_int   = (long *)((long)ed->m_int   + ptr_eventsData);
		ed->m_normDate=(char *)((long)ed->m_normDate + ptr_eventsData);
		// do not repeat!
		ed->m_eventFlags |= EV_DESERIALIZED;
		return ed;
	}
	// strange
	//char *xx=NULL; *xx=0; 
	// none found
	return NULL;
}

void EventIdBits::set ( class HttpRequest *r , char *cgiparm ) {

	// clear the bits
	clear();

	// get cgi parm, if exists, if not just return
	long plen;
	char *p = r->getString ( cgiparm , &plen );
	if ( ! p || plen == 0 ) return;

	// right now 256 bits = 32 bytes = 64 chars. bad length?
	if ( plen != 64 ) return;

	// bits were encoded as hexadecimal chars, one byte being encoded
	// as two hexadecimal chars
	char *dst = (char *)m_bits;
	hexToBin ( p , plen , dst );
}

bool printConfirmFlags ( SafeBuf *sb , long confirmed  ) {
	if ( confirmed & CF_ACCEPT ) 
		sb->safePrintf("cfaccept,");
	if ( confirmed & CF_REJECT ) 
		sb->safePrintf("cfreject,");
	if ( confirmed & CF_TITLE ) 
		sb->safePrintf("cftitle,");
	if ( confirmed & CF_VENUE ) 
		sb->safePrintf("cfvenue,");
	if ( confirmed & CF_VENUE_NONE ) 
		sb->safePrintf("cfvenuenone,");
	// backup over last comma?
	if ( confirmed ) {
		// backup
		sb->advance(-1);
		// null just in case
		sb->m_buf[sb->m_length] = '\0';
	}
	return true;
}

bool printEventDescFlags ( SafeBuf *sb, long dflags ) {
	if ( dflags & EDF_TITLE )
		sb->safePrintf("edftitle,");
	if ( dflags & EDF_IN_SUMMARY )
		sb->safePrintf("edfinsummary,");
	if ( dflags & EDF_DATE_ONLY )
		sb->safePrintf("edfdateonly,");
	if ( dflags & EDF_HASEVENTADDRESS )
		sb->safePrintf("edfhaseventaddress,");
	if ( dflags & EDF_JUST_PLACE )
		sb->safePrintf("edfjustplace,");
	if ( dflags & EDF_PLACE_TYPE )
		sb->safePrintf("<font color=blue>edfplacetype</font>,");
	if ( dflags & EDF_BEST_PLACE_NAME )
		sb->safePrintf("edfbestplacename,");
	if ( dflags & EDF_PRETTY )
		sb->safePrintf("edfpretty,");
	if ( dflags & EDF_CONFIRMED_DESCR )
		sb->safePrintf("edfconfirmeddescr,");
	if ( dflags & EDF_CONFIRMED_NOT_DESCR )
		sb->safePrintf("edfconfirmednotdescr,");
#ifdef _USETURKS_
	/*
	if ( dflags & EDF_TURK_TITLE )
		sb->safePrintf("edfturktitle,");
	if ( dflags & EDF_TURK_VENUE )
		sb->safePrintf("edfturkvenue,");
	if ( dflags & EDF_TURK_TITLE_CANDIDATE )
		sb->safePrintf("edfturktitlecandidate,");
	if ( dflags & EDF_TURK_VENUE_CANDIDATE )
		sb->safePrintf("edfturkvenuecandidate,");
	*/
#endif

	if ( dflags & EDF_MENU_CRUFT )
		sb->safePrintf("edfmenucruft,");
	if ( dflags & EDF_HASEVENTDATE )
		sb->safePrintf("edfhaseventdate,");
	if ( dflags & EDF_INDEXABLE )
		sb->safePrintf("edfindexable,");
	if ( dflags & EDF_FARDUP )
		sb->safePrintf("edffardup,");
	if ( dflags & EDF_FARDUPPHONE )
		sb->safePrintf("edffardupphone,");
	if ( dflags & EDF_FARDUPPRICE )
		sb->safePrintf("edffardupprice,");
	if ( dflags & EDF_SUBEVENTBROTHER )
		sb->safePrintf("edfsubeventbrother,");
	if ( dflags & EDF_HAS_PRICE )
		sb->safePrintf("edfhasprice,");
	if ( dflags & EDF_INTITLETAG )
		sb->safePrintf("edfintitletag,");
	if ( dflags & EDF_FBPICSQUARE )
		sb->safePrintf("fbpicsquare,");
	if ( dflags & EDF_TAGS )
		sb->safePrintf("edftags,");

	// backup over last comma?
	if ( dflags ) {
		// backup
		sb->advance(-1);
		// null just in case
		sb->m_buf[sb->m_length] = '\0';
	}
	return true;
}	

bool printEventFlags ( SafeBuf *sb , evflags_t f ) {

	char *start = sb->getBuf();

	if ( f & EV_BAD_EVENT )
		sb->safePrintf("hasbadevent,");
	if ( f & EV_SENTSPANSMULTEVENTS )
		sb->safePrintf("sentspansmultevents,");
	if ( f & EV_ADCH32DUP )
		sb->safePrintf("adch32dup,");
	if ( f & EV_HADNOTITLE )
		sb->safePrintf("hadnotitle,");
	if ( f & EV_DEDUPED )
		sb->safePrintf("deduped,");
	//if ( f & EV_BAD_STORE_HOURS )
	//	sb->safePrintf("badstorehours,");
	if ( f & EV_STORE_HOURS )
		sb->safePrintf("storehours,");
	if ( f & EV_SUBSTORE_HOURS )
		sb->safePrintf("substorehours,");
	if ( f & EV_HASTITLEWORDS )
		sb->safePrintf("hastitlewords,");
	if ( f & EV_HASTITLEFIELD )
		sb->safePrintf("hastitlefield,");
	if ( f & EV_HASTITLESUBSENT )
		sb->safePrintf("hastitlesubsent,");
	if ( f & EV_HASTITLEBYVOTES )
		sb->safePrintf("hastitlebyvotes,");
	if ( f & EV_HASTITLEWITHCOLON )
		sb->safePrintf("hastitlewithcolon,");
	if ( f & EV_HASTIGHTDATE )
		sb->safePrintf("hastightdate,");
	if ( f & EV_INCRAZYTABLE )
		sb->safePrintf("incrazytable,");
	if ( f & EV_DO_NOT_INDEX )
		sb->safePrintf("donotindex,");
	if ( f & EV_VENUE_DEFAULT )
		sb->safePrintf("venuedefault,");
	//if ( f & EV_MENU )
	//	sb->safePrintf("eventmenu,");
	if ( f & EV_MISSING_LOCATION )
		sb->safePrintf("missinglocation,");
	if ( f & EV_REGISTRATION )
		sb->safePrintf("registrationdate,");
	if ( f & EV_TICKET_PLACE )
		sb->safePrintf("registrationplace,");
	if ( f & EV_MULT_LOCATIONS )
		sb->safePrintf("multlocations,");
	if ( f & EV_UNVERIFIED_LOCATION )
		sb->safePrintf("unverifiedlocation,");
	if ( f & EV_AMBIGUOUS_LOCATION )
		sb->safePrintf("ambiguouslocation,");
	if ( f & EV_SPECIAL_DUP )
		sb->safePrintf("specialdup,");
	if ( f & EV_NO_LOCATION )
		sb->safePrintf("nolocation,");
	// if the address we wanted to use turned out to have a phone number
	// in its sections, and we had a different phone number in ours,
	// then we can not be compatible with him...
	if ( f & EV_NOT_COMPATIBLE )
		sb->safePrintf("incompatiblelocation,");
	if ( f & EV_OUTLINKED_TITLE )
		sb->safePrintf("outlinkedtitle,");
	if ( f & EV_IS_POBOX )
		sb->safePrintf("ispobox,");
	if ( f & EV_OLD_EVENT )
		sb->safePrintf("oldtimes,");
	// we set this if could be clock
	if ( f & EV_SAMEDAY )
		sb->safePrintf("samedayeventmaybeclock,");
	if ( f & EV_LONGDURATION )
		sb->safePrintf("longduration,");
	if ( f & EV_PRIVATE )
		sb->safePrintf("private,");
	if ( f & EV_FACEBOOK )
		sb->safePrintf("facebook,");
	if ( f & EV_STUBHUB )
		sb->safePrintf("stubhub,");
	if ( f & EV_EVENTBRITE )
		sb->safePrintf("eventbrite,");
	if ( f & EV_HIDEGUESTLIST )
		sb->safePrintf("hideguestlist,");
	if ( f & EV_LATLONADDRESS )
		sb->safePrintf("latlonaddress,");
	// if event is in the year before m_year0 then the intersetion
	// in Dates::getIntervals2() will be empty! so just let user know
	// that it might be an old year too!
	//if ( (f & EV_EMPTY_TIMES) && 
	//     m_dates->m_year0>ev->m_date->m_year &&
	//     ev->m_date->m_year > 0 )
	//	sb->safePrintf("oldertimes,");	
	//else if ( f & EV_EMPTY_TIMES )
	if ( f & EV_EMPTY_TIMES )
		sb->safePrintf("emptyoroldtimes,");
	if ( f & EV_NO_YEAR )
		sb->safePrintf("daynumbutnoyear,");

	//if ( sb->getBuf() == start && f ) { char *xx=NULL;*xx=0; }

	// backup over last comma?
	if ( sb->getBuf() != start ) {
		// backup
		sb->advance(-1);
		// null just in case
		sb->m_buf[sb->m_length] = '\0';
	}

	return true;
}
