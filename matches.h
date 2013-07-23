// Matt Wells, copyright Jan 2007

#ifndef _MATCHES_FUNC_H_
#define _MATCHES_FUNC_H_

// use these routines for matching any of a list of substrings in the haystack.
// the Matches array is the list of substrings to match in the "haystack". this
// should be *very* fast.
class Needle {
public:
	char *m_string;
	char  m_stringSize;
	char  m_id;
	// if m_isSection is true, getMatch() only matches if haystack 
	// ptr is < linkPos
	char  m_isSection; 
	long  m_count;
	char *m_stringSave;
	char  m_stringSizeSave;
	char *m_firstMatch;
	// used by XmlDoc::getEventSummary()
	float m_score;
	// used by XmlDoc::getEventSummary() to point to query word #
	long  m_qwn;
};


char *getMatches2 ( Needle *needles          , 
		    long    numNeedles       ,
		    char   *haystack         , 
		    long    haystackSize     ,
		    char   *linkPos          ,
		    long   *n                ,
		    bool    stopAtFirstMatch ,
		    bool   *hadPreMatch      ,
		    bool    saveQuickTables  ,
		    long    niceness         );



#endif 
