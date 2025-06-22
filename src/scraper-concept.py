# This script is a proof-of-concept for scraping a user's download history from Nexus Mods.
# It uses Selenium to automate a Firefox browser, navigate through pages, and extract mod information.

import json
import time
from pathlib import Path
from selenium import webdriver
from selenium.common.exceptions import NoSuchElementException, TimeoutException
from selenium.webdriver.common.by import By
from selenium.webdriver.support.ui import WebDriverWait
from selenium.webdriver.support import expected_conditions as EC
from selenium.webdriver.firefox.options import Options

# --- Browser Configuration ---
# These options are used to configure the Firefox driver for performance and to avoid interruptions.
options = Options()
options.add_argument("--log-level=3")
options.add_argument("--mute-audio")
options.add_argument("--no-sandbox")
options.add_argument("--disable-dev-shm-usage")
options.set_preference("http.response.timeout", 1)
options.set_preference('permissions.default.stylesheet', 2)
options.set_preference('permissions.default.image', 2)
options.set_preference('dom.ipc.plugins.enabled.libflashplayer.so', 'false')
options.set_capability('pageLoadStrategy', 'eager')

# Initialize the Firefox driver and a WebDriverWait instance for handling dynamic content.
Firefox = webdriver.Firefox(options=options)
wait = WebDriverWait(Firefox, 3)

def scrape_files(mod_url):
    """
    Scrapes a single mod page for its files and their requirements.

    This function navigates to the 'files' tab of a mod page, extracts download links for each main file,
    and recursively scrapes the requirements for each of those files.

    Args:
        mod_url (str): The URL of the mod page to scrape.

    Returns:
        dict: A dictionary containing the mod's URL and a nested dictionary of its files,
              including requirements and the actual filename.
    """
    result = {
        'url': mod_url,
        'files': dict()
    }
    Firefox.get(mod_url+'?tab=files')
    wait.until(EC.visibility_of_element_located((By.XPATH, '//dt[contains(@id, "file-expander-header")]')))
    filelinks = dict()
    # Iterate through each file entry on the page.
    # The XPath targets the headers for each file section.
    for i, e in enumerate(Firefox.find_elements(by='xpath', value='//dt[contains(@id, "file-expander-header")]')):
        try:
            e.find_element(by='xpath', value='./div/i')
        except NoSuchElementException:
            continue
        name = e.get_attribute('data-name')
        try:
            # This locator is brittle. It assumes the structure and order of elements.
            link = Firefox.find_element('xpath', f'(//dd[@class="clearfix open"])[{i+1}]//a[contains(@class, "btn inline-flex")]').get_attribute('href').replace('&nmm=1', '')
        except NoSuchElementException:
            # If the link isn't found, assume the section is collapsed and click to open it.
            Firefox.find_element(by='xpath', value=f'(//div[@class="acc-status"])[{i+1}]').click()
            # This assumes the newly opened link is the last one on the page, which can be unreliable.
            link = Firefox.find_elements('xpath', '//dd[@class="clearfix open"]//a[contains(@class, "btn inline-flex")]')[-1].get_attribute('href').replace('&nmm=1', '')
        finally:
            filelinks[name] = link
    
    # For each file found, visit its download page to get requirements and the real filename.
    for name, link in filelinks.items():
        requirements = None
        Firefox.get(link)
        # Check if the download is blocked by a requirements pop-up.
        if 'ModRequirementsPopUp' in link:
            required = {e.find_element(by='xpath', value='./span').text: e.get_attribute('href') for e in Firefox.find_elements(by='xpath', value='//div[@class="mod-requirements-tab-content"]/ul/li/a')}
            modlink = Firefox.find_element('xpath', '//div[@class="mod-requirements-tab-content"]/a[@class="btn"]').get_attribute('href')
            if required:
                # Recursively scrape the requirements. This could lead to deep recursion or infinite loops.
                requirements = {k: scrape_files(v) for k, v in required.items() if v.startswith('https://www.nexusmods.com/')}
                if not requirements:
                    requirements = None
            # After handling requirements, proceed to the actual download page.
            Firefox.get(modlink)
        # Get the filename from the download page header.
        file = wait.until(EC.visibility_of_element_located((By.XPATH, '//div[@class="header"]'))).text.splitlines()[0]
        result['files'][name] = {'requirements': requirements, 'filename': file}
    
    return result

# --- Main Execution Logic ---

# 1. Load cookies to log in.
# The script requires a cookies.json file from a logged-in Nexus Mods session.
Firefox.get('https://www.nexusmods.com/')
for i in json.loads(Path('D:/cookies.json').read_text()):
    Firefox.add_cookie({'name': i['name'], 'value': i['value']})

# 2. Navigate to the user's download history.
Firefox.get('https://www.nexusmods.com/users/myaccount?tab=download+history')
time.sleep(10) # A fixed wait, which is unreliable. Explicit waits are better.

# 3. Scrape all mod links from the download history, paginating through all pages.
mods = dict()
while True:
    for mod in Firefox.find_elements(by='xpath', value="//div[@class='tracking-title']/a"):
        name = mod.text
        url = mod.get_attribute('href')
        mods[name] = url
    next_button = Firefox.find_element(by='xpath', value="//div/a[contains(@class, 'paginate_button next')]")
    # Check if the 'next' button is disabled to know when to stop.
    if next_button.get_attribute('class') != 'paginate_button next disabled':
        next_button.click()
    else:
        break

modfiles = dict()
# 4. Iterate through the collected mods and scrape the files for each one.
for name, url in mods.items():
    game = url.split('/')[3]
    if not modfiles.get(game):
        modfiles[game] = {
            name: scrape_files(url)
        }
    else:
        modfiles[game][name] = scrape_files(url)

# 5. Save the collected data to a JSON file.
Path('D:/nexusmods_downloaded.json').write_text(json.dumps(modfiles, ensure_ascii=False, indent=4), encoding='utf8')